#include "Weight_Function_Integration.hh"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "Angular_Discretization.hh"
#include "Basis_Function.hh"
#include "Check.hh"
#include "Cross_Section.hh"
#include "Dimensional_Moments.hh"
#include "Energy_Discretization.hh"
#include "KD_Tree.hh"
#include "Material.hh"
#include "Meshless_Function.hh"
#include "Meshless_Normalization.hh"
#include "Solid_Geometry.hh"
#include "Quadrature_Rule.hh"
#include "Weak_Spatial_Discretization.hh"
#include "Weight_Function.hh"

using namespace std;

Weight_Function_Integration::
Weight_Function_Integration(int number_of_points,
                            shared_ptr<Weak_Spatial_Discretization_Options> options,
                            vector<shared_ptr<Basis_Function> > const &bases,
                            vector<shared_ptr<Weight_Function> > const &weights,
                            shared_ptr<Solid_Geometry> solid,
                            vector<vector<double> > limits,
                            vector<int> dimensional_cells):
    options_(options),
    number_of_points_(number_of_points),
    bases_(bases),
    weights_(weights),
    solid_(solid)
{
    Assert(bases.size() == number_of_points_);
    Assert(weights.size() == number_of_points_);
    Assert(solid);

    // Create mesh
    mesh_ = make_shared<Mesh>(*this,
                              solid->dimension(),
                              limits,
                              dimensional_cells);

    // Check to ensure sufficient number of cells
    if (mesh_->number_of_background_cells_ < number_of_points_)
    {
        cerr << "more background integration cells recommended" << endl;
        cerr << "number of points:\t" << number_of_points_ << endl;
        cerr << "number of cells:\t" << mesh_->number_of_background_cells_ << endl;
    }
    
    // Get normalization information
    apply_basis_normalization_ = bases[0]->function()->depends_on_neighbors();
    apply_weight_normalization_ = weights[0]->function()->depends_on_neighbors();

    if (apply_basis_normalization_)
    {
        basis_normalization_ = bases[0]->function()->normalization();
    }
    if (apply_weight_normalization_)
    {
        weight_normalization_ = weights[0]->function()->normalization();
    }

    // Get information on Galerkin vs Petrov-Galerkin discretization
    identical_basis_functions_ = options_->identical_basis_functions == Weak_Spatial_Discretization_Options::Identical_Basis_Functions::TRUE;
    
    // Get angular and energy discretizations
    shared_ptr<Material> test_material = solid_->material(weights_[0]->position());
    angular_ = test_material->angular_discretization();
    energy_ = test_material->energy_discretization();
    if (options->weighting == Weak_Spatial_Discretization_Options::Weighting::FLUX)
    {
        Assert(options->flux_coefficients.size()
               == (number_of_points_
                   * energy_->number_of_groups()
                   * angular_->number_of_moments()));
    }
}

void Weight_Function_Integration::
perform_integration()
{
    // Initialize integrals to zero
    vector<Weight_Function::Integrals> integrals;
    initialize_integrals(integrals);
    
    // Initialize materials to zero
    vector<Material_Data> materials;
    initialize_materials(materials);
    
    // Perform volume integration
    perform_volume_integration(integrals,
                               materials);

    // Perform surface integration
    perform_surface_integration(integrals);

    // Put results into weight functions and materials
    put_integrals_into_weight(integrals,
                              materials);
}

void Weight_Function_Integration::
perform_volume_integration(vector<Weight_Function::Integrals> &integrals,
                           vector<Material_Data> &materials) const
{
    // Integral values should be initialized to zero in perform_integration()
    for (int i = 0; i < mesh_->number_of_background_cells_; ++i)
    {
        // Get cell data
        Mesh::Cell &cell = mesh_->cells_[i];
        
        // Get quadrature
        int number_of_ordinates;
        vector<vector<double> > ordinates;
        vector<double> weights;
        get_volume_quadrature(i,
                              number_of_ordinates,
                              ordinates,
                              weights);
        
        // Get connectivity information
        vector<vector<int> > weight_basis_indices;
        get_cell_basis_indices(cell,
                               weight_basis_indices);

        // Get center positions
        vector<vector<double> > weight_centers;
        vector<vector<double> > basis_centers;
        get_weight_centers(cell.weight_indices,
                           weight_centers);
        if (identical_basis_functions_)
        {
            basis_centers = weight_centers;
        }
        else
        {
            get_basis_centers(cell.basis_indices,
                              basis_centers);
        }                  
        
        for (int q = 0; q < number_of_ordinates; ++q)
        {
            // Get position
            vector<double> const &position = ordinates[q];

            // Get material and basis/weight values at quadrature point
            vector<double> b_val;
            vector<vector<double> > b_grad;
            vector<double> w_val;
            vector<vector<double> > w_grad;
            shared_ptr<Material> point_material;
            get_volume_values(cell,
                              position,
                              basis_centers,
                              weight_centers,
                              b_val,
                              b_grad,
                              w_val,
                              w_grad,
                              point_material);
            
            // Add these values to the overall integrals
            add_volume_weight(cell,
                              weights[q],
                              w_val,
                              w_grad,
                              integrals);
            add_volume_basis_weight(cell,
                                    weights[q],
                                    b_val,
                                    b_grad,
                                    w_val,
                                    w_grad,
                                    weight_basis_indices,
                                    integrals);
            add_volume_material(cell,
                                weights[q],
                                b_val,
                                w_val,
                                w_grad,
                                weight_basis_indices,
                                point_material,
                                materials);
        }
    }

    // Normalize materials
    normalize_materials(materials);
}

void Weight_Function_Integration::
normalize_materials(vector<Material_Data> &materials) const
{
    // Normalize only if requested
    if (options_->normalized)
    {
        int number_of_groups = energy_->number_of_groups();
        int number_of_scattering_moments = angular_->number_of_scattering_moments();
        int number_of_moments = angular_->number_of_moments();
        
        for (int i = 0; i < number_of_points_; ++i)
        {
            shared_ptr<Weight_Function> weight = weights_[i];
            int number_of_dimensional_moments = weight->dimensional_moments()->number_of_dimensional_moments();
            Material_Data &material = materials[i];
            
            switch (options_->weighting)
            {
            case Weak_Spatial_Discretization_Options::Weighting::POINT:
                AssertMsg(false, "point weighting not compatible with external integration");
                break;
            case Weak_Spatial_Discretization_Options::Weighting::FLAT:
                // Fallthrough intentional
            case Weak_Spatial_Discretization_Options::Weighting::BASIS:
                for (int d = 0; d < number_of_dimensional_moments; ++d)
                {
                    // Total cross section
                    for (int g = 0; g < number_of_groups; ++g)
                    {
                        int kt = d + number_of_dimensional_moments * g;
                        material.sigma_t[kt] /= material.norm[d];
                    }

                    // Fission cross section
                    for (int g1 = 0; g1 < number_of_groups; ++g1)
                    {
                        for (int g2 = 0; g2 < number_of_groups; ++g2)
                        {
                            int kf = d + number_of_dimensional_moments * (g2 + number_of_groups * g1);
                            material.sigma_f[kf] /= material.norm[d];
                        }
                    }

                    // Scattering cross section
                    for (int l = 0; l < number_of_scattering_moments; ++l)
                    {
                        for (int g1 = 0; g1 < number_of_groups; ++g1)
                        {
                            for (int g2 = 0; g2 < number_of_groups; ++g2)
                            {
                                int ks = d + number_of_dimensional_moments * (g2 + number_of_groups * (g1 + number_of_groups * l));
                                material.sigma_s[ks] /= material.norm[d];
                            }
                        }
                    }
                }
                break;
            case Weak_Spatial_Discretization_Options::Weighting::FLUX:
                for (int d = 0; d < number_of_dimensional_moments; ++d)
                {
                    // Total cross section
                    for (int m = 0; m < number_of_moments; ++m)
                    {
                        for (int g = 0; g < number_of_groups; ++g)
                        {
                            int kt = d + number_of_dimensional_moments * (g + number_of_groups * m);
                            int kn = d + number_of_dimensional_moments * (g + number_of_groups * m);
                            material.sigma_t[kt] /= material.norm[kn];
                        }
                    }

                    // Fission cross section
                    for (int g1 = 0; g1 < number_of_groups; ++g1)
                    {
                        for (int g2 = 0; g2 < number_of_groups; ++g2)
                        {
                            int kf = d + number_of_dimensional_moments * (g2 + number_of_groups * g1);
                            int kn = d + number_of_dimensional_moments * (g2 + number_of_groups * 0);
                            material.sigma_f[kf] /= material.norm[kn];
                        }
                    }

                    // Scattering cross section
                    for (int m = 0; m < number_of_moments; ++m)
                    {
                        for (int g1 = 0; g1 < number_of_groups; ++g1)
                        {
                            for (int g2 = 0; g2 < number_of_groups; ++g2)
                            {
                                int ks = d + number_of_dimensional_moments * (g2 + number_of_groups * (g1 + number_of_groups * m));
                                int kn = d + number_of_dimensional_moments * (g2 + number_of_groups * m);
                                material.sigma_s[ks] /= material.norm[kn];
                            }
                        }
                    }
                }
                break;
            case Weak_Spatial_Discretization_Options::Weighting::FULL:
                // No normalization needed
                break;
            } // switch options->weighting
        } // points
    } // if normalized
}

void Weight_Function_Integration::
add_volume_weight(Mesh::Cell const &cell,
                  double quad_weight,
                  vector<double> const &w_val,
                  vector<vector<double> > const &w_grad,
                  vector<Weight_Function::Integrals> &integrals) const
{
    for (int i = 0; i < cell.number_of_weight_functions; ++i)
    {
        // Add weight integral
        int w_ind = cell.weight_indices[i];
        integrals[w_ind].iv_w[0] += quad_weight * w_val[i];

        // Add derivative of weight integral
        for (int d = 0; d < mesh_->dimension_; ++d)
        {
            integrals[w_ind].iv_dw[d] += quad_weight * w_grad[i][d];
        }
    }
}

void Weight_Function_Integration::
add_volume_basis_weight(Mesh::Cell const &cell,
                        double quad_weight,
                        vector<double> const &b_val,
                        vector<vector<double> > const &b_grad,
                        vector<double> const &w_val,
                        vector<vector<double> > const &w_grad,
                        vector<vector<int> > const &weight_basis_indices,
                        vector<Weight_Function::Integrals> &integrals) const
{
    int dimension = mesh_->dimension_;
    for (int i = 0; i < cell.number_of_weight_functions; ++i)
    {
        // Add weight integral
        int w_ind = cell.weight_indices[i];
        
        for (int j = 0; j < cell.number_of_basis_functions; ++j)
        {
            int b_ind = cell.basis_indices[j];
            int w_b_ind = weight_basis_indices[i][j];
            
            if (w_b_ind != Weight_Function::Errors::DOES_NOT_EXIST)
            {
                // Basis-weight integral
                integrals[w_ind].iv_b_w[w_b_ind]
                    += quad_weight * w_val[i] * b_val[j];

                // Derivative integrals
                for (int d1 = 0; d1 < dimension; ++d1)
                {
                    int k1 = d1 + dimension * w_b_ind;
                    integrals[w_ind].iv_b_dw[k1]
                        += quad_weight * w_grad[i][d1] * b_val[j];
                    integrals[w_ind].iv_db_w[k1]
                        += quad_weight * w_val[i] * b_grad[j][d1];

                    for (int d2 = 0; d2 < dimension; ++d2)
                    {
                        int k2 = d1 + dimension * (d2 + dimension * w_b_ind);
                        integrals[w_ind].iv_db_dw[k2]
                            += quad_weight * w_grad[i][d2] * b_grad[j][d1];
                    }
                }
            }
            
        }
    }
}

void Weight_Function_Integration::
get_cross_sections(shared_ptr<Material> material,
                   vector<double> &sigma_t,
                   vector<double> &sigma_s,
                   vector<double> &chi_nu_sigma_f,
                   vector<double> &internal_source) const
{
    sigma_t = material->sigma_t()->data();
    sigma_s = material->sigma_s()->data();
    internal_source = material->internal_source()->data();

    if (material->sigma_f()->dependencies().energy
        == Cross_Section::Dependencies::Energy::GROUP_TO_GROUP)
    {
        chi_nu_sigma_f = material->sigma_f()->data();
    }
    else
    {
        int number_of_groups = energy_->number_of_groups();
        vector<double> nu = material->nu()->data();
        vector<double> sigma_f = material->sigma_f()->data();
        vector<double> chi = material->chi()->data();

        chi_nu_sigma_f.resize(number_of_groups * number_of_groups);
        for (int gt = 0; gt < number_of_groups; ++gt)
        {
            for (int gf = 0; gf < number_of_groups; ++gf)
            {
                int k = gf + number_of_groups * gt;
                
                chi_nu_sigma_f[k] = chi[gt] * nu[gf] * sigma_f[gf];
            }
        }
    }
}

void Weight_Function_Integration::
add_volume_material(Mesh::Cell const &cell,
                    double quad_weight,
                    vector<double> const &b_val,
                    vector<double> const &w_val,
                    vector<vector<double> > const &w_grad,
                    vector<vector<int> > const &weight_basis_indices,
                    shared_ptr<Material> point_material,
                    vector<Material_Data> &materials) const
{
    // Get size information
    int number_of_groups = energy_->number_of_groups();
    int number_of_scattering_moments = angular_->number_of_scattering_moments();
    int number_of_moments = angular_->number_of_moments();
    vector<int> const scattering_indices = angular_->scattering_indices();
    
    // Get cross sections
    vector<double> sigma_t;
    vector<double> sigma_s;
    vector<double> sigma_f;
    vector<double> internal_source;
    get_cross_sections(point_material,
                       sigma_t,
                       sigma_s,
                       sigma_f,
                       internal_source);
                       
    // Get flux
    vector<double> flux;
    if (options_->weighting == Weak_Spatial_Discretization_Options::Weighting::FLUX)
    {
        get_flux(cell,
                 b_val,
                 flux);
    }

    // Get data
    int number_of_dimensional_moments = weights_[0]->dimensional_moments()->number_of_dimensional_moments();
    
    // Add value to each of the weight functions
    switch (options_->weighting)
    {
    case Weak_Spatial_Discretization_Options::Weighting::POINT:
        AssertMsg(false, "point weighting not compatible with external integration");
        break;
    case Weak_Spatial_Discretization_Options::Weighting::FLAT:
        for (int i = 0; i < cell.number_of_weight_functions; ++i)
        {
            // Get weight function data
            int w_ind = cell.weight_indices[i];
            Material_Data &material = materials[w_ind];
        
            for (int d = 0; d < number_of_dimensional_moments; ++d)
            {
                double wid = d == 0 ? w_val[i] : w_grad[i][d - 1];

                // Norm 
                material.norm[d] += wid * quad_weight;
                
                for (int g = 0; g < number_of_groups; ++g)
                {
                    // Total cross section and internal source
                    int kt = d + number_of_dimensional_moments * g;
                    
                    material.sigma_t[kt] += sigma_t[g] * wid * quad_weight;
                    material.internal_source[kt] += internal_source[g] * wid * quad_weight;
                    
                    for (int g2 = 0; g2 < number_of_groups; ++g2)
                    {
                        // Fission cross section
                        int kf = d + number_of_dimensional_moments * (g2 + number_of_groups * g);
                        int kg = g2 + number_of_groups * g;
                        
                        material.sigma_f[kf] += sigma_f[kg] * wid * quad_weight;
                        
                        for (int l = 0; l < number_of_scattering_moments; ++l)
                        {
                            // Scattering cross section
                            int ks = d + number_of_dimensional_moments * (g2 + number_of_groups * (g + number_of_groups * l));
                            int ks0 = g2 + number_of_groups * (g + number_of_groups * l);
                            material.sigma_s[ks] += sigma_s[ks0] * wid * quad_weight;
                        }
                    }
                }
            }
        }
        break;
    case Weak_Spatial_Discretization_Options::Weighting::FLUX:
        for (int i = 0; i < cell.number_of_weight_functions; ++i)
        {
            // Get weight function data
            int w_ind = cell.weight_indices[i];
            Material_Data &material = materials[w_ind];
        
            for (int d = 0; d < number_of_dimensional_moments; ++d)
            {
                double wid = d == 0 ? w_val[i] : w_grad[i][d - 1];
                
                for (int g = 0; g < number_of_groups; ++g)
                {
                    // Internal source
                    int kn = d + number_of_dimensional_moments * g;
                    material.internal_source[kn] += internal_source[g] * wid * quad_weight;

                    for (int g2 = 0; g2 < number_of_groups; ++g2)
                    {
                        // Fission cross section
                        int m0 = 0;
                        int kf = d + number_of_dimensional_moments * (g2 + number_of_groups * g);
                        int kx = g2 + number_of_groups * m0;
                        int kg = g2 + number_of_groups * g;

                        material.sigma_f[kf] += sigma_f[kg] * flux[kx] * wid * quad_weight;
                    }
                    
                    for (int m = 0; m < number_of_moments; ++m)
                    {
                        // Get scattering index
                        int l = scattering_indices[m];
                        
                        // Total cross section and norm
                        int kt = d + number_of_dimensional_moments * (g + number_of_groups * m);
                        int kx = g + number_of_groups * m;
                        material.sigma_t[kt] += flux[kx] * sigma_t[g] * wid * quad_weight;
                        material.norm[kt] += flux[kx] * wid * quad_weight;
                        
                        for (int g2 = 0; g2 < number_of_groups; ++g2)
                        {
                            // Scattering cross section
                            int ks = d + number_of_dimensional_moments * (g2 + number_of_groups * (g + number_of_groups * m));
                            int ks0 = g2 + number_of_groups * (g + number_of_groups * l);
                            int kxs = g2 + number_of_groups * m;
                            
                            material.sigma_s[ks] += sigma_s[ks0] * flux[kxs] * wid * quad_weight;
                        }
                    }
                }
            }
        }
        break;
    case Weak_Spatial_Discretization_Options::Weighting::FULL:
        for (int i = 0; i < cell.number_of_weight_functions; ++i)
        {
            // Get weight function data
            int w_ind = cell.weight_indices[i];
            Material_Data &material = materials[w_ind];
        
            for (int d = 0; d < number_of_dimensional_moments; ++d)
            {
                double wid = d == 0 ? w_val[i] : w_grad[i][d - 1];

                // Internal source (does not depend on basis functions)
                for (int g = 0; g < number_of_groups; ++g)
                {
                    int kn = d + number_of_dimensional_moments * g;
                    
                    material.internal_source[kn] += internal_source[g] * wid * quad_weight;
                }

                // Material integrals that depend on basis function
                for (int j = 0; j < cell.number_of_basis_functions; ++j)
                {
                    int b_ind = cell.basis_indices[j];
                    int w_b_ind = weight_basis_indices[i][j];
                    
                    if (w_b_ind != Weight_Function::Errors::DOES_NOT_EXIST)
                    {
                        // Total cross section
                        for (int g = 0; g < number_of_groups; ++g)
                        {
                            int kt = d + number_of_dimensional_moments * (g + number_of_groups * w_b_ind);
                            material.sigma_t[kt] += sigma_t[g] * b_val[j] * wid * quad_weight;
                        }
                        
                        // Fission cross section
                        for (int g1 = 0; g1 < number_of_groups; ++g1)
                        {
                            for (int g2 = 0; g2 < number_of_groups; ++g2)
                            {
                                int kf = d + number_of_dimensional_moments * (g2 + number_of_groups * (g1 + number_of_groups * w_b_ind));
                                int kg = g2 + number_of_groups * g1;
                                
                                material.sigma_f[kf] += sigma_f[kg] * b_val[j] * wid * quad_weight;
                            }
                        }
                        
                        // Scattering cross section
                        for (int l = 0; l < number_of_scattering_moments; ++l)
                        {
                            for (int g1 = 0; g1 < number_of_groups; ++g1)
                            {
                                for (int g2 = 0; g2 < number_of_groups; ++g2)
                                {
                                    int ks = d + number_of_dimensional_moments * (g2 + number_of_groups * (g1 + number_of_groups * (l + number_of_scattering_moments * w_b_ind)));
                                    int kg = g2 + number_of_groups * (g1 + number_of_groups * l);

                                    material.sigma_s[ks] += sigma_s[kg] * b_val[j] * wid * quad_weight;
                                }
                            }
                        }
                    }
                }
            }
        }
        break;
    case Weak_Spatial_Discretization_Options::Weighting::BASIS:
        // Perform internal source integration
        for (int i = 0; i < cell.number_of_weight_functions; ++i)
        {
            // Get weight function data
            int w_ind = cell.weight_indices[i];
            Material_Data &material = materials[w_ind];
            
            for (int d = 0; d < number_of_dimensional_moments; ++d)
            {
                double wid = d == 0 ? w_val[i] : w_grad[i][d - 1];
                
                for (int g = 0; g < number_of_groups; ++g)
                {
                    // Total cross section and internal source
                    int kt = d + number_of_dimensional_moments * g;
                    
                    material.internal_source[kt] += internal_source[g] * wid * quad_weight;
                }
            }
        }

        // Perform cross section integration
        for (int i = 0; i < cell.number_of_basis_functions; ++i)
        {
            // Get basis function data
            int b_ind = cell.basis_indices[i];
            Material_Data &material = materials[b_ind];
            double bas = b_val[i];

            // Integrals should be the same for all dimensional moments:
            // Could optimize later
            for (int d = 0; d < number_of_dimensional_moments; ++d)
            {
                material.norm[d] += bas * quad_weight;
                
                for (int g = 0; g < number_of_groups; ++g)
                {
                    // Total cross section and internal source
                    int kt = d + number_of_dimensional_moments * g;
                    
                    material.sigma_t[kt] += sigma_t[g] * bas * quad_weight;
                    
                    for (int g2 = 0; g2 < number_of_groups; ++g2)
                    {
                        // Fission cross section
                        int kf = d + number_of_dimensional_moments * (g2 + number_of_groups * g);
                        int kg = g2 + number_of_groups * g;
                        
                        material.sigma_f[kf] += sigma_f[kg] * bas * quad_weight;
                        
                        for (int l = 0; l < number_of_scattering_moments; ++l)
                        {
                            // Scattering cross section
                            int ks = d + number_of_dimensional_moments * (g2 + number_of_groups * (g + number_of_groups * l));
                            int ks0 = g2 + number_of_groups * (g + number_of_groups * l);
                            material.sigma_s[ks] += sigma_s[ks0] * bas * quad_weight;
                        }
                    }
                }
            }
        }
        break;
    } // switch options->weighting
}

void Weight_Function_Integration::
perform_surface_integration(vector<Weight_Function::Integrals> &integrals) const
{
    // Integral values should be initialized to zero in perform_integration()
    for (int i = 0; i < mesh_->number_of_background_surfaces_; ++i)
    {
        // Get surface data
        Mesh::Surface &surface = mesh_->surfaces_[i];

        // Get local weight function indices for this surface
        vector<int> weight_surface_indices;
        get_weight_surface_indices(surface,
                                   weight_surface_indices);

        // Get local basis function indices for all weights
        vector<vector<int> > weight_basis_indices;
        get_surface_basis_indices(surface,
                                  weight_basis_indices);
        
        // Get quadrature
        int number_of_ordinates;
        vector<vector<double> > ordinates;
        vector<double> weights;
        get_surface_quadrature(i,
                               number_of_ordinates,
                               ordinates,
                               weights);

        // Get centers
        vector<vector<double> > weight_centers;
        vector<vector<double> > basis_centers;
        get_weight_centers(surface.weight_indices,
                           weight_centers);
        if (identical_basis_functions_)
        {
            basis_centers = weight_centers;
        }
        else
        {
            get_basis_centers(surface.basis_indices,
                              basis_centers);
        }                  
        
        for (int q = 0; q < number_of_ordinates; ++q)
        {
            // Get position
            vector<double> const &position = ordinates[q];

            // Get basis/weight values at quadrature point
            vector<double> b_val;
            vector<double> w_val;
            get_surface_values(surface,
                               position,
                               basis_centers,
                               weight_centers,
                               b_val,
                               w_val);

            // Perform integration
            add_surface_weight(surface,
                               weights[q],
                               w_val,
                               weight_surface_indices,
                               integrals);
            add_surface_basis_weight(surface,
                                     weights[q],
                                     b_val,
                                     w_val,
                                     weight_surface_indices,
                                     weight_basis_indices,
                                     integrals);
        }
    }
}

void Weight_Function_Integration::
add_surface_weight(Mesh::Surface const &surface,
                   double quad_weight,
                   vector<double> const &w_val,
                   vector<int> const &weight_surface_indices,
                   vector<Weight_Function::Integrals> &integrals) const
{
    for (int i = 0; i < surface.number_of_weight_functions; ++i)
    {
        int w_ind = surface.weight_indices[i]; // global weight index
        int w_s_ind = weight_surface_indices[i]; // local surface index for weight
        
        if (w_s_ind != Weight_Function::Errors::DOES_NOT_EXIST)
        {
            integrals[w_ind].is_w[w_s_ind]
                += quad_weight * w_val[i];
        }
    }
}

void Weight_Function_Integration::
add_surface_basis_weight(Mesh::Surface const &surface,
                         double quad_weight,
                         vector<double> const &b_val,
                         vector<double> const &w_val,
                         vector<int> const &weight_surface_indices,
                         vector<vector<int> > const &weight_basis_indices,
                         vector<Weight_Function::Integrals> &integrals) const
{
    for (int i = 0; i < surface.number_of_weight_functions; ++i)
    {
        int w_ind = surface.weight_indices[i]; // global weight index
        int w_s_ind = weight_surface_indices[i]; // local surface index for weight
        int number_of_boundary_surfaces = weights_[w_ind]->number_of_boundary_surfaces();
        
        if (w_s_ind != Weight_Function::Errors::DOES_NOT_EXIST)
        {
            for (int j = 0; j < surface.number_of_basis_functions; ++j)
            {
                int w_b_ind = weight_basis_indices[i][j]; // local basis index for weight
                
                if (w_b_ind != Weight_Function::Errors::DOES_NOT_EXIST)
                {
                    int i_ind = w_s_ind + number_of_boundary_surfaces * w_b_ind;
                    integrals[w_ind].is_b_w[i_ind]
                        += quad_weight * w_val[i] * b_val[j];
                }
            }
        }
    }
}

void Weight_Function_Integration::
get_weight_surface_indices(Mesh::Surface const &surface,
                           vector<int> &indices) const
{
    indices.assign(surface.number_of_weight_functions, Weight_Function::Errors::DOES_NOT_EXIST);
    for (int i = 0; i < surface.number_of_weight_functions; ++i)
    {
        shared_ptr<Weight_Function> weight = weights_[surface.weight_indices[i]];
        indices[i] = weight->local_surface_index(surface.dimension,
                                                 surface.normal);
    }
}

void Weight_Function_Integration::
put_integrals_into_weight(vector<Weight_Function::Integrals> const &integrals,
                          vector<Material_Data> const &material_data)
{
    for (int i = 0; i < number_of_points_; ++i)
    {
        // Get material from material data
        shared_ptr<Material> material;
        get_material(i,
                     material_data[i],
                     material);
        
        // Set the integrals to the weight functions
        weights_[i]->set_integrals(integrals[i],
                                   material);
    }
}

void Weight_Function_Integration::
get_material(int index,
             Material_Data const &material_data,
             shared_ptr<Material> &material) const
{
    // Get weight function data
    int number_of_basis_functions = weights_[index]->number_of_basis_functions();
    
    // Get dependencies : all initialized to none
    Cross_Section::Dependencies sigma_t_deps;
    Cross_Section::Dependencies sigma_s_deps;
    Cross_Section::Dependencies nu_deps;
    Cross_Section::Dependencies sigma_f_deps;
    Cross_Section::Dependencies chi_deps;
    Cross_Section::Dependencies internal_source_deps;
    Cross_Section::Dependencies norm_deps;

    // Set energy dependence
    sigma_t_deps.energy = Cross_Section::Dependencies::Energy::GROUP;
    sigma_s_deps.energy = Cross_Section::Dependencies::Energy::GROUP_TO_GROUP;
    sigma_f_deps.energy = Cross_Section::Dependencies::Energy::GROUP_TO_GROUP;
    internal_source_deps.energy = Cross_Section::Dependencies::Energy::GROUP;

    // Set weighting dependent dependencies
    switch (options_->weighting)
    {
    case Weak_Spatial_Discretization_Options::Weighting::POINT:
        AssertMsg(false, "point weighting not compatible with external integration");
        break;
    case Weak_Spatial_Discretization_Options::Weighting::FLAT:
        sigma_s_deps.angular = Cross_Section::Dependencies::Angular::SCATTERING_MOMENTS;
        break;
    case Weak_Spatial_Discretization_Options::Weighting::FLUX:
        sigma_t_deps.angular = Cross_Section::Dependencies::Angular::MOMENTS;
        sigma_s_deps.angular = Cross_Section::Dependencies::Angular::MOMENTS;
        norm_deps.angular = Cross_Section::Dependencies::Angular::MOMENTS;
        norm_deps.energy = Cross_Section::Dependencies::Energy::GROUP;
        break;
    case Weak_Spatial_Discretization_Options::Weighting::FULL:
        sigma_s_deps.angular = Cross_Section::Dependencies::Angular::SCATTERING_MOMENTS;
        sigma_t_deps.spatial = Cross_Section::Dependencies::Spatial::BASIS_WEIGHT;
        sigma_s_deps.spatial = Cross_Section::Dependencies::Spatial::BASIS_WEIGHT;
        sigma_f_deps.spatial = Cross_Section::Dependencies::Spatial::BASIS_WEIGHT;
        sigma_t_deps.number_of_basis_functions = number_of_basis_functions;
        sigma_s_deps.number_of_basis_functions = number_of_basis_functions;
        sigma_f_deps.number_of_basis_functions = number_of_basis_functions;
        break;
    case Weak_Spatial_Discretization_Options::Weighting::BASIS:
        sigma_s_deps.angular = Cross_Section::Dependencies::Angular::SCATTERING_MOMENTS;
        sigma_t_deps.spatial = Cross_Section::Dependencies::Spatial::BASIS;
        sigma_s_deps.spatial = Cross_Section::Dependencies::Spatial::BASIS;
        sigma_f_deps.spatial = Cross_Section::Dependencies::Spatial::BASIS;
        norm_deps.spatial = Cross_Section::Dependencies::Spatial::BASIS;
        break;
    }
    
    // Set SUPG dependencies
    if (options_->include_supg)
    {
        sigma_t_deps.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
        sigma_s_deps.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
        sigma_f_deps.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
        internal_source_deps.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
        norm_deps.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
    }
    
    // Get cross sections
    shared_ptr<Cross_Section> sigma_t
        = make_shared<Cross_Section>(sigma_t_deps,
                                     angular_,
                                     energy_,
                                     material_data.sigma_t);
    shared_ptr<Cross_Section> sigma_s
        = make_shared<Cross_Section>(sigma_s_deps,
                                     angular_,
                                     energy_,
                                     material_data.sigma_s);
    shared_ptr<Cross_Section> nu
        = make_shared<Cross_Section>(nu_deps,
                                     angular_,
                                     energy_,
                                     material_data.nu);
    shared_ptr<Cross_Section> sigma_f
        = make_shared<Cross_Section>(sigma_f_deps,
                                     angular_,
                                     energy_,
                                     material_data.sigma_f);
    shared_ptr<Cross_Section> chi
        = make_shared<Cross_Section>(chi_deps,
                                     angular_,
                                     energy_,
                                     material_data.chi);
    shared_ptr<Cross_Section> internal_source
        = make_shared<Cross_Section>(internal_source_deps,
                                     angular_,
                                     energy_,
                                     material_data.internal_source);
    shared_ptr<Cross_Section> norm
        = make_shared<Cross_Section>(norm_deps,
                                     angular_,
                                     energy_,
                                     material_data.norm);

    // Create material
    material = make_shared<Material>(index,
                                     angular_,
                                     energy_,
                                     sigma_t,
                                     sigma_s,
                                     nu,
                                     sigma_f,
                                     chi,
                                     internal_source,
                                     norm);
}

void Weight_Function_Integration::
get_volume_values(Mesh::Cell const &cell,
                  vector<double> const &position,
                  vector<vector<double> > const &basis_centers,
                  vector<vector<double> > const &weight_centers,
                  vector<double> &b_val,
                  vector<vector<double> > &b_grad,
                  vector<double> &w_val,
                  vector<vector<double> > &w_grad,
                  shared_ptr<Material> &point_material) const
{
    // Initialize values 
    b_val.resize(cell.number_of_basis_functions);
    b_grad.resize(cell.number_of_basis_functions);
    w_val.resize(cell.number_of_weight_functions);
    w_grad.resize(cell.number_of_weight_functions);

    // Get material at quadrature point
    point_material = solid_->material(position);
    
    // Get values for weight functions at quadrature point
    for (int j = 0; j < cell.number_of_weight_functions; ++j)
    {
        shared_ptr<Meshless_Function> func = weights_[cell.weight_indices[j]]->function()->base_function();

        w_val[j] = func->value(position);
        w_grad[j] = func->gradient_value(position);
    }
    
    // Normalize weight functions
    if (apply_weight_normalization_)
    {
        weight_normalization_->get_gradient_values(position,
                                                   weight_centers,
                                                   w_val,
                                                   w_grad,
                                                   w_val,
                                                   w_grad);
    }
    
    if (identical_basis_functions_)
    {
        b_val = w_val;
        b_grad = w_grad;
    }
    else
    {
        // Get values for basis functions at quadrature point
        for (int j = 0; j < cell.number_of_basis_functions; ++j)
        {
            shared_ptr<Meshless_Function> func = bases_[cell.basis_indices[j]]->function()->base_function();
                
            b_val[j] = func->value(position);
            b_grad[j] = func->gradient_value(position);
        }

        // Normalize basis functions
        if (apply_basis_normalization_)
        {
            basis_normalization_->get_gradient_values(position,
                                                      basis_centers,
                                                      b_val,
                                                      b_grad,
                                                      b_val,
                                                      b_grad);
        }
    }
 }

void Weight_Function_Integration::
get_surface_values(Mesh::Surface const &surface,
                   vector<double> const &position,
                   vector<vector<double> > const &basis_centers,
                   vector<vector<double> > const &weight_centers,
                   vector<double> &b_val,
                   vector<double> &w_val) const
{
    // Initialize values
    b_val.resize(surface.number_of_basis_functions);
    w_val.resize(surface.number_of_weight_functions);
    
    // Get values for weight functions at quadrature point
    for (int j = 0; j < surface.number_of_weight_functions; ++j)
    {
        shared_ptr<Meshless_Function> func = weights_[surface.weight_indices[j]]->function()->base_function();
                
        w_val[j] = func->value(position);
    }
    
    // Normalize weight functions
    if (apply_weight_normalization_)
    {
        weight_normalization_->get_values(position,
                                          weight_centers,
                                          w_val,
                                          w_val);
    }

    if (identical_basis_functions_)
    {
        b_val = w_val;
    }
    else
    {
        // Get values for basis functions at quadrature point
        for (int j = 0; j < surface.number_of_basis_functions; ++j)
        {
            shared_ptr<Meshless_Function> func = bases_[surface.basis_indices[j]]->function()->base_function();
                
            b_val[j] = func->value(position);
        }
        if (apply_basis_normalization_)
        {
            basis_normalization_->get_values(position,
                                             basis_centers,
                                             b_val,
                                             b_val);
        }
    }
 }

void Weight_Function_Integration::
get_surface_quadrature(int i,
                       int &number_of_ordinates,
                       vector<vector<double> > &ordinates,
                       vector<double> &weights) const
{
    // Get limits of integration
    Mesh::Surface const &surface = mesh_->surfaces_[i];
    Mesh::Cell const &cell = mesh_->cells_[surface.neighboring_cell];
    vector<vector<double> > const &limits = cell.limits;
    int const number_of_integration_ordinates = options_->integration_ordinates;
    int const dx = 0;
    int const dy = 1;
    int const dz = 2;
    int const min = 0;
    int const max = 1;

    // Get temporary integration data
    Quadrature_Rule::Quadrature_Type quad_type = Quadrature_Rule::Quadrature_Type::GAUSS_LEGENDRE;
    vector<double> ordinates_x;
    vector<double> ordinates_y;
    vector<double> ordinates_z;

    // Get quadrature
    switch (mesh_->dimension_)
    {
    case 1:
        number_of_ordinates = 1;
        if (surface.normal < 0)
        {
            // At negative x boundary
            ordinates.assign(1, vector<double>(1, limits[dx][min]));
            weights.assign(1, 1.);
        }
        else
        {
            // At positive x boundary
            ordinates.assign(1, vector<double>(1, limits[dx][max]));
            weights.assign(1, 1.);
        }
        break;
    case 2:
        switch (surface.dimension)
        {
        case dx:
            Quadrature_Rule::cartesian_1d(quad_type,
                                          number_of_integration_ordinates,
                                          limits[dy][min],
                                          limits[dy][max],
                                          ordinates_y,
                                          weights);
            number_of_ordinates = weights.size();
            if (surface.normal < 0)
            {
                // At negative x boundary
                ordinates_x.assign(number_of_ordinates,
                                   limits[dx][min]);
            }
            else
            {
                // At positive x boundary
                ordinates_x.assign(number_of_ordinates,
                                   limits[dx][max]);
            }
            break;
        case dy:
            Quadrature_Rule::cartesian_1d(quad_type,
                                          number_of_integration_ordinates,
                                          limits[dx][min],
                                          limits[dx][max],
                                          ordinates_x,
                                          weights);
            number_of_ordinates = weights.size();
            if (surface.normal < 0)
            {
                // At negative x boundary
                ordinates_y.assign(number_of_ordinates,
                                   limits[dy][min]);
            }
            else
            {
                // At positive x boundary
                ordinates_y.assign(number_of_ordinates,
                                   limits[dy][max]);
            }
            break;
        }
        Quadrature_Rule::convert_to_position_2d(ordinates_x,
                                                ordinates_y,
                                                ordinates);
        break;
    case 3:
        switch (surface.dimension)
        {
        case dx:
            Quadrature_Rule::cartesian_2d(quad_type,
                                          quad_type,
                                          number_of_integration_ordinates,
                                          number_of_integration_ordinates,
                                          limits[dy][min],
                                          limits[dy][max],
                                          limits[dz][min],
                                          limits[dz][max],
                                          ordinates_y,
                                          ordinates_z,
                                          weights);
            number_of_ordinates = weights.size();
            if (surface.normal < 0)
            {
                // At negative x boundary
                ordinates_x.assign(number_of_ordinates,
                                   limits[dx][min]);
            }
            else
            {
                // At positive x boundary
                ordinates_x.assign(number_of_ordinates,
                                   limits[dx][max]);
            }
            break;
        case dy:
            Quadrature_Rule::cartesian_2d(quad_type,
                                          quad_type,
                                          number_of_integration_ordinates,
                                          number_of_integration_ordinates,
                                          limits[dx][min],
                                          limits[dx][max],
                                          limits[dz][min],
                                          limits[dz][max],
                                          ordinates_x,
                                          ordinates_z,
                                          weights);
            number_of_ordinates = weights.size();
            if (surface.normal < 0)
            {
                // At negative y boundary
                ordinates_y.assign(number_of_ordinates,
                                   limits[dy][min]);
            }
            else
            {
                // At positive y boundary
                ordinates_y.assign(number_of_ordinates,
                                   limits[dx][max]);
            }
            break;
        case dz:
            Quadrature_Rule::cartesian_2d(quad_type,
                                          quad_type,
                                          number_of_integration_ordinates,
                                          number_of_integration_ordinates,
                                          limits[dx][min],
                                          limits[dx][max],
                                          limits[dy][min],
                                          limits[dy][max],
                                          ordinates_x,
                                          ordinates_y,
                                          weights);
            number_of_ordinates = weights.size();
            if (surface.normal < 0)
            {
                // At negative z boundary
                ordinates_z.assign(number_of_ordinates,
                                   limits[dz][min]);
            }
            else
            {
                // At positive z boundary
                ordinates_z.assign(number_of_ordinates,
                                   limits[dz][max]);
            }
            break;
        }
        Quadrature_Rule::convert_to_position_3d(ordinates_x,
                                                ordinates_y,
                                                ordinates_z,
                                                ordinates);
        break;
    }
}

void Weight_Function_Integration::
get_volume_quadrature(int i,
                      int &number_of_ordinates,
                      vector<vector<double> > &ordinates,
                      vector<double> &weights) const
{
    // Get limits of integration
    Mesh::Cell const &cell = mesh_->cells_[i];
    vector<vector<double> > const &limits = cell.limits;
    int const number_of_integration_ordinates = options_->integration_ordinates;
    int const dx = 0;
    int const dy = 1;
    int const dz = 2;
    int const min = 0;
    int const max = 1;

    // Initialize temporary integration data
    Quadrature_Rule::Quadrature_Type quad_type = Quadrature_Rule::Quadrature_Type::GAUSS_LEGENDRE;
    vector<double> ordinates_x;
    vector<double> ordinates_y;
    vector<double> ordinates_z;

    // Get quadrature
    switch (mesh_->dimension_)
    {
    case 1:
        Quadrature_Rule::cartesian_1d(quad_type,
                                      number_of_integration_ordinates,
                                      limits[dx][min],
                                      limits[dx][max],
                                      ordinates_x,
                                      weights);
        Quadrature_Rule::convert_to_position_1d(ordinates_x,
                                                ordinates);
        break;
    case 2:
        Quadrature_Rule::cartesian_2d(quad_type,
                                      quad_type,
                                      number_of_integration_ordinates,
                                      number_of_integration_ordinates,
                                      limits[dx][min],
                                      limits[dx][max],
                                      limits[dy][min],
                                      limits[dy][max],
                                      ordinates_x,
                                      ordinates_y,
                                      weights);
        Quadrature_Rule::convert_to_position_2d(ordinates_x,
                                                ordinates_y,
                                                ordinates);
        break;
    case 3:
        Quadrature_Rule::cartesian_3d(quad_type,
                                      quad_type,
                                      quad_type,
                                      number_of_integration_ordinates,
                                      number_of_integration_ordinates,
                                      number_of_integration_ordinates,
                                      limits[dx][min],
                                      limits[dx][max],
                                      limits[dy][min],
                                      limits[dy][max],
                                      limits[dz][min],
                                      limits[dz][max],
                                      ordinates_x,
                                      ordinates_y,
                                      ordinates_z,
                                      weights);
        Quadrature_Rule::convert_to_position_3d(ordinates_x,
                                                ordinates_y,
                                                ordinates_z,
                                                ordinates);
        break;
    }

    // Set number of ordinates
    number_of_ordinates = weights.size();
}

void Weight_Function_Integration::
initialize_integrals(vector<Weight_Function::Integrals> &integrals) const
{
    integrals.resize(number_of_points_);
    for (int i = 0; i < number_of_points_; ++i)
    {
        shared_ptr<Weight_Function> weight = weights_[i];
        int number_of_boundary_surfaces = weight->number_of_boundary_surfaces();
        int number_of_basis_functions = weight->number_of_basis_functions();
        Weight_Function::Integrals &local_integrals = integrals[i];
        local_integrals.is_w.assign(number_of_boundary_surfaces, 0.);
        local_integrals.is_b_w.assign(number_of_boundary_surfaces * number_of_basis_functions, 0);
        local_integrals.iv_w.assign(1, 0.);
        local_integrals.iv_dw.assign(mesh_->dimension_, 0);
        local_integrals.iv_b_w.assign(number_of_basis_functions, 0);
        local_integrals.iv_b_dw.assign(number_of_basis_functions * mesh_->dimension_, 0);
        local_integrals.iv_db_w.assign(number_of_basis_functions * mesh_->dimension_, 0);
        local_integrals.iv_db_dw.assign(number_of_basis_functions * mesh_->dimension_ * mesh_->dimension_, 0);
    }
}

void Weight_Function_Integration::
initialize_materials(vector<Material_Data> &materials) const
{
    // Get material data
    int number_of_groups = energy_->number_of_groups();
    int number_of_scattering_moments = angular_->number_of_scattering_moments();
    int number_of_moments = angular_->number_of_moments();
    
    // Initialize materials
    materials.resize(number_of_points_);
    for (int i = 0; i < number_of_points_; ++i)
    {
        shared_ptr<Weight_Function> weight = weights_[i];
        int number_of_dimensional_moments = weight->dimensional_moments()->number_of_dimensional_moments();
        int number_of_basis_functions = weight->number_of_basis_functions();
        Material_Data &material = materials[i];

        material.nu.assign(1, 1.);
        material.chi.assign(1, 1.);
        material.internal_source.assign(number_of_dimensional_moments
                                        * number_of_groups, 0);
        switch (options_->weighting)
        {
        case Weak_Spatial_Discretization_Options::Weighting::POINT:
            AssertMsg(false, "point weighting not compatible with external integration");
            break;
        case Weak_Spatial_Discretization_Options::Weighting::FLAT:
            material.sigma_t.assign(number_of_dimensional_moments
                                    * number_of_groups, 0.);
            material.sigma_s.assign(number_of_dimensional_moments
                                    * number_of_groups
                                    * number_of_groups
                                    * number_of_scattering_moments, 0.);
            material.sigma_f.assign(number_of_dimensional_moments
                                    * number_of_groups
                                    * number_of_groups, 0.);
            material.norm.assign(number_of_dimensional_moments, 0.);
            break;
        case Weak_Spatial_Discretization_Options::Weighting::FLUX:
            material.sigma_t.assign(number_of_dimensional_moments
                                    * number_of_groups
                                    * number_of_moments, 0.);
            material.sigma_s.assign(number_of_dimensional_moments
                                    * number_of_groups
                                    * number_of_groups
                                    * number_of_moments, 0.);
            material.sigma_f.assign(number_of_dimensional_moments
                                    * number_of_groups
                                    * number_of_groups, 0.);
            material.norm.assign(number_of_dimensional_moments
                                 * number_of_groups
                                 * number_of_moments, 0.);
            break;
        case Weak_Spatial_Discretization_Options::Weighting::FULL:
            material.sigma_t.assign(number_of_basis_functions
                                    * number_of_dimensional_moments
                                    * number_of_groups, 0.);
            material.sigma_s.assign(number_of_basis_functions
                                    * number_of_dimensional_moments
                                    * number_of_groups
                                    * number_of_groups
                                    * number_of_scattering_moments, 0.);
            material.sigma_f.assign(number_of_basis_functions
                                    * number_of_dimensional_moments
                                    * number_of_groups
                                    * number_of_groups, 0.);
            material.norm.assign(number_of_dimensional_moments, 1.);
            break;
        case Weak_Spatial_Discretization_Options::Weighting::BASIS:
            material.sigma_t.assign(number_of_dimensional_moments
                                    * number_of_groups, 0.);
            material.sigma_s.assign(number_of_dimensional_moments
                                    * number_of_groups
                                    * number_of_groups
                                    * number_of_scattering_moments, 0.);
            material.sigma_f.assign(number_of_dimensional_moments
                                    * number_of_groups
                                    * number_of_groups, 0.);
            material.norm.assign(number_of_dimensional_moments, 0.);
            break;
        } // switch options->weighting
    } // for points
}

void Weight_Function_Integration::
get_cell_basis_indices(Mesh::Cell const &cell,
                       vector<vector<int> > &indices) const
{
    indices.assign(cell.number_of_weight_functions,
                   vector<int>(cell.number_of_basis_functions, -1));
    for (int i = 0; i < cell.number_of_weight_functions; ++i)
    {
        shared_ptr<Weight_Function> weight = weights_[cell.weight_indices[i]];
        for (int j = 0; j < cell.number_of_basis_functions; ++j)
        {
            indices[i][j] = weight->local_basis_index(cell.basis_indices[j]);
        }
    }
}

void Weight_Function_Integration::
get_surface_basis_indices(Mesh::Surface const &surface,
                          vector<vector<int> > &indices) const
{
    indices.assign(surface.number_of_weight_functions,
                   vector<int>(surface.number_of_basis_functions, -1));
    for (int i = 0; i < surface.number_of_weight_functions; ++i)
    {
        shared_ptr<Weight_Function> weight = weights_[surface.weight_indices[i]];
        for (int j = 0; j < surface.number_of_basis_functions; ++j)
        {
            indices[i][j] = weight->local_basis_index(surface.basis_indices[j]);
        }
    }
}

void Weight_Function_Integration::
get_basis_centers(vector<int> const &basis_indices,
                  vector<vector<double> > &center_positions) const
{
    int number_of_basis_functions = basis_indices.size();
    center_positions.resize(number_of_basis_functions);
    for (int i = 0; i < number_of_basis_functions; ++i)
    {
        center_positions[i] = bases_[basis_indices[i]]->position();
    }
}

void Weight_Function_Integration::
get_weight_centers(vector<int> const &weight_indices,
                   vector<vector<double> > &center_positions) const
{
    int number_of_weight_functions = weight_indices.size();
    center_positions.resize(number_of_weight_functions);
    for (int i = 0; i < number_of_weight_functions; ++i)
    {
        center_positions[i] = weights_[weight_indices[i]]->position();
    }
}

void Weight_Function_Integration::
get_flux(Mesh::Cell const &cell,
         vector<double> const &b_val,
         vector<double> &flux) const
{
    // Get size information
    int const number_of_groups = energy_->number_of_groups();
    int const number_of_moments = angular_->number_of_moments();

    // Get coefficients
    vector<double> const &coefficients = options_->flux_coefficients;
    double const sff = options_->scalar_flux_fraction;

    // Calculate flux
    flux.assign(number_of_groups * number_of_moments, 0.);
    int const m0 = 0;
    for (int i = 0; i < cell.number_of_basis_functions; ++i)
    {
        int const j = cell.basis_indices[i];
        for (int g = 0; g < number_of_groups; ++g)
        {
            int const k_sf = g + number_of_groups * (m0 + number_of_moments * j); // global scalar flux coefficient index
            for (int m = 0; m < number_of_moments; ++m)
            {
                int const k_f = g + number_of_groups * m; // local flux index
                int const k_c = g + number_of_groups * (m + number_of_moments * j); // global coefficient index
                flux[k_f] += b_val[i] * ((1 - sff) * coefficients[k_c] + sff * coefficients[k_sf]);
            }
        }
    }
    
    // Ensure all scalar flux values are positive for weighting
    for (int g = 0; g < number_of_groups; ++g)
    {
        int const k_sf = g + number_of_groups * m0;
        flux[k_sf] = abs(flux[k_sf]);
    }
}


Weight_Function_Integration::Mesh::
Mesh(Weight_Function_Integration const &wfi,
     int dimension,
     vector<vector<double> > limits,
     vector<int> dimensional_cells):
    wfi_(wfi),
    dimension_(dimension),
    limits_(limits),
    dimensional_cells_(dimensional_cells)
{
    initialize_mesh();
    initialize_connectivity();
}

void Weight_Function_Integration::Mesh::
initialize_mesh()
{
    vector<vector<double> > positions;
    
    // Check sizes
    Assert(dimensional_cells_.size() == dimension_);
    Assert(limits_.size() == dimension_);
    
    // Get total number of nodes
    dimensional_nodes_.resize(dimension_);
    number_of_background_nodes_ = 1;
    number_of_background_cells_ = 1;
    for (int d = 0; d < dimension_; ++d)
    {
        Assert(dimensional_cells_[d] >= 1);
        dimensional_nodes_[d] = dimensional_cells_[d] + 1;
        number_of_background_nodes_ *= dimensional_nodes_[d];
        number_of_background_cells_ *= dimensional_cells_[d];
    }
    
    // Get intervals between cells
    intervals_.resize(dimension_);
    for (int d = 0; d < dimension_; ++d)
    {
        intervals_[d] = (limits_[d][1] - limits_[d][0]) / static_cast<double>(dimensional_cells_[d]);
    }
    

    // Initialize nodes
    nodes_.resize(number_of_background_nodes_);
    switch (dimension_)
    {
    case 1:
    {
        int const di = 0;
        for (int i = 0; i < dimensional_nodes_[0]; ++i)
        {
            double index = i;
            Node &node = nodes_[i];
            node.position = {limits_[di][0] + intervals_[di] * i};
        }
        break;
    }
    case 2:
    {
        int const di = 0;
        int const dj = 1;
        for (int i = 0; i < dimensional_nodes_[0]; ++i)
        {
            for (int j = 0; j < dimensional_nodes_[1]; ++j)
            {
                int index = j + dimensional_nodes_[1] * i;
                Node &node = nodes_[index];
                node.position = {limits_[di][0] + intervals_[di] * i,
                                 limits_[dj][0] + intervals_[dj] * j};
            }
        }
        break;
    }
    case 3:
    {
        int const di = 0;
        int const dj = 1;
        int const dk = 2;
        for (int i = 0; i < dimensional_nodes_[0]; ++i)
        {
            for (int j = 0; j < dimensional_nodes_[1]; ++j)
            {
                for (int k = 0; k < dimensional_nodes_[2]; ++k)
                {
                    int index = k + dimensional_nodes_[2] * (j + dimensional_nodes_[1] * i);
                    Node &node = nodes_[index];
                    node.position = {limits_[di][0] + intervals_[di] * i,
                                     limits_[dj][0] + intervals_[dj] * j,
                                     limits_[dk][0] + intervals_[dk] * k};
                }
            }
            break;
        }
    }
    default:
        AssertMsg(false, "dimension (" + to_string(dimension_) +  ") not found");
    }

    // Initialize cells
    cells_.resize(number_of_background_cells_);
    switch (dimension_)
    {
    case 1:
    {
        int di = 0;
        for (int i = 0; i < dimensional_cells_[di]; ++i)
        {
            int index = i;
            Cell &cell = cells_[index];
            
            // Set upper and lower limits
            cell.limits.resize(dimension_);
            for (int d = 0; d < dimension_; ++d)
            {
                int l0 = index;
                int l1 = l0 + 1;
                cell.limits[d] = {limits_[d][0] + intervals_[d] * l0,
                                  limits_[d][0] + intervals_[d] * l1};
            }
                
            // Set neighboring nodes and cells
            for (int ni = i; ni <= i + 1; ++ni)
            {
                int n_index = ni;
                Node &node = nodes_[n_index];
                node.neighboring_cells.push_back(index);
                cell.neighboring_nodes.push_back(n_index);
            }
        }
        break;
    }
    case 2:
    {
        int di = 0;
        int dj = 1;
        for (int i = 0; i < dimensional_cells_[di]; ++i)
        {
            for (int j = 0; j < dimensional_cells_[dj]; ++j)
            {
                int index = j + dimensional_cells_[dj] * i;
                vector<int> indices = {i, j};
                Cell &cell = cells_[index];
                
                // Set upper and lower limits
                cell.limits.resize(dimension_);
                for (int d = 0; d < dimension_; ++d)
                {
                    int l0 = indices[d];
                    int l1 = l0 + 1;
                    cell.limits[d] = {limits_[d][0] + intervals_[d] * l0,
                                      limits_[d][0] + intervals_[d] * l1};
                }
                
                // Set neighboring nodes and cells
                for (int ni = i; ni <= i + 1; ++ni)
                {
                    for (int nj = j; nj <= j + 1; ++nj)
                    {
                        int n_index = nj + dimensional_nodes_[dj] * ni;
                        Node &node = nodes_[n_index];
                        node.neighboring_cells.push_back(index);
                        cell.neighboring_nodes.push_back(n_index);
                    }
                }
            }
        }
        break;
    }
    case 3:
    {
        int di = 0;
        int dj = 1;
        int dk = 2;
        for (int i = 0; i < dimensional_cells_[di]; ++i)
        {
            for (int j = 0; j < dimensional_cells_[dj]; ++j)
            {
                for (int k = 0; k < dimensional_cells_[dk]; ++k)
                {
                    int index = k + dimensional_cells_[dk] * (j + dimensional_cells_[dj] * i);
                    vector<int> indices = {i, j, k};
                    Cell &cell = cells_[index];
                    
                    // Set neighboring nodes and cells
                    for (int ni = i; ni <= i + 1; ++ni)
                    {
                        for (int nj = j; nj <= j + 1; ++nj)
                        {
                            for (int nk = k; nk <= k + 1; ++nk)
                            {
                                int n_index = nk + dimensional_nodes_[dk] * (nj + dimensional_nodes_[dj] * ni);
                                Node &node = nodes_[n_index];
                                node.neighboring_cells.push_back(index);
                                cell.neighboring_nodes.push_back(n_index);
                            }
                        }
                    }
                
                    // Set upper and lower limits
                    cell.limits.resize(dimension_);
                    for (int d = 0; d < dimension_; ++d)
                    {
                        int l0 = indices[d];
                        int l1 = l0 + 1;
                        cell.limits[d] = {limits_[d][0] + intervals_[d] * l0,
                                          limits_[d][0] + intervals_[d] * l1};
                    }
                }
            }
        }
        break;
    }
    default:
        AssertMsg(false, "dimension (" + to_string(dimension_) + ") not found");
    }

    // Initialize boundary surfaces
    switch (dimension_)
    {
    case 1:
    {
        number_of_background_surfaces_ = 2;
        surfaces_.resize(number_of_background_surfaces_);
        for (int i = 0; i < number_of_background_surfaces_; ++i)
        {
            surfaces_[i].dimension = 0;
        }
        surfaces_[0].normal = -1;
        surfaces_[1].normal = 1;
        surfaces_[0].neighboring_cell = 0;
        surfaces_[1].neighboring_cell = number_of_background_cells_ - 1;
        nodes_[0].neighboring_surfaces.push_back(0);
        nodes_[dimensional_nodes_[0] - 1].neighboring_surfaces.push_back(1);
        break;
    }
    case 2:
    {
        int const di = 0;
        int const dj = 1;
        number_of_background_surfaces_ = 2 * (dimensional_cells_[di]
                                              + dimensional_cells_[dj]);
        surfaces_.clear();
        surfaces_.reserve(number_of_background_surfaces_);
        int index = 0;
        
        // Positive and negative (p = 0, 1) x boundaries
        for (int p = 0; p < 2; ++p)
        {
            int i = p == 0 ? 0 : dimensional_cells_[di] - 1;
            int ni = p == 0 ? 0 : dimensional_nodes_[di] - 1;
            double normal = p == 0 ? -1 : 1;
            for (int j = 0; j < dimensional_cells_[dj]; ++j)
            {
                Surface surface;
                surface.dimension = di;
                surface.normal = normal;
                surface.neighboring_cell = j + dimensional_cells_[dj] * i;
                surfaces_.push_back(surface);
                
                for (int nj = j; nj <= j + 1; ++nj)
                {
                    int n_index = nj + dimensional_nodes_[dj] * ni;
                    Node &node = nodes_[n_index];
                    node.neighboring_surfaces.push_back(index);
                }

                index += 1;
            }
        }
        
        // Positive and negative (p = 0, 1) y boundaries
        for (int p = 0; p < 2; ++p)
        {
            int j = p == 0 ? 0 : dimensional_cells_[dj] - 1;
            int nj = p == 0 ? 0 : dimensional_nodes_[dj] - 1;
            double normal = p == 0 ? -1 : 1;
            for (int i = 0; i < dimensional_cells_[di]; ++i)
            {
                Surface surface;
                surface.dimension = dj;
                surface.normal = normal;
                surface.neighboring_cell = j + dimensional_cells_[dj] * i;
                surfaces_.push_back(surface);
                
                for (int ni = i; ni <= i + 1; ++ni)
                {
                    int n_index = nj + dimensional_nodes_[dj] * ni;
                    Node &node = nodes_[n_index];
                    node.neighboring_surfaces.push_back(index);
                }
                
                index += 1;
            }
        }
        break;
    }
    case 3:
    {
        int const di = 0;
        int const dj = 1;
        int const dk = 2;
        number_of_background_surfaces_ = 2 * (dimensional_cells_[0] * dimensional_cells_[1]
                                              + dimensional_cells_[0] * dimensional_cells_[2]
                                              + dimensional_cells_[1] * dimensional_cells_[2]);
        surfaces_.clear();
        surfaces_.reserve(number_of_background_surfaces_);
        int index = 0;
        
        // Positive and negative (p = 0, 1) x boundaries
        for (int p = 0; p < 2; ++p)
        {
            int i = p == 0 ? 0 : dimensional_cells_[di] - 1;
            int ni = p == 0 ? 0 : dimensional_nodes_[di] - 1;
            double normal = p == 0 ? -1 : 1;
            for (int j = 0; j < dimensional_cells_[dj]; ++j)
            {
                for (int k = 0; k < dimensional_cells_[dk]; ++k)
                {
                    Surface surface;
                    surface.dimension = di;
                    surface.normal = normal;
                    surface.neighboring_cell = k + dimensional_cells_[dk] * (j + dimensional_cells_[dj]* i);
                    surfaces_.push_back(surface);
                    
                    for (int nj = j; nj <= j + 1; ++nj)
                    {
                        for (int nk = k; nk <= k + 1; ++nk)
                        {
                            int n_index = nk + dimensional_nodes_[dk] * (nj + dimensional_nodes_[dj] * i);
                            Node &node = nodes_[n_index];
                            node.neighboring_surfaces.push_back(index);
                        }
                    }

                    index += 1;
                }
            }
        }

        // Positive and negative (p = 0, 1) y boundaries
        for (int p = 0; p < 2; ++p)
        {
            int j = p == 0 ? 0 : dimensional_cells_[dj] - 1;
            int nj = p == 0 ? 0 : dimensional_nodes_[dj] - 1;
            double normal = p == 0 ? -1 : 1;
            for (int i = 0; j < dimensional_cells_[di]; ++i)
            {
                for (int k = 0; k < dimensional_cells_[dk]; ++k)
                {
                    Surface surface;
                    surface.dimension = dj;
                    surface.normal = normal;
                    surface.neighboring_cell = k + dimensional_cells_[dk] * (j + dimensional_cells_[dj]* i);
                    surfaces_.push_back(surface);
                    
                    for (int ni = i; ni <= i + 1; ++ni)
                    {
                        for (int nk = k; nk <= k + 1; ++nk)
                        {
                            int n_index = nk + dimensional_nodes_[dk] * (nj + dimensional_nodes_[dj] * i);
                            Node &node = nodes_[n_index];
                            node.neighboring_surfaces.push_back(index);
                        }
                    }

                    index += 1;
                }
            }
        }

        // Positive and negative (p = 0, 1) y boundaries
        for (int p = 0; p < 2; ++p)
        {
            int k = p == 0 ? 0 : dimensional_cells_[dk] - 1;
            int nk = p == 0 ? 0 : dimensional_nodes_[dk] - 1;
            double normal = p == 0 ? -1 : 1;
            for (int i = 0; i < dimensional_cells_[di]; ++i)
            {
                for (int j = 0; j < dimensional_cells_[dj]; ++j)
                {
                    Surface surface;
                    surface.dimension = dk;
                    surface.normal = normal;
                    surface.neighboring_cell = k + dimensional_cells_[dk] * (j + dimensional_cells_[dj]* i);
                    surfaces_.push_back(surface);
                    
                    for (int ni = i; ni <= i + 1; ++ni)
                    {
                        for (int nj = j; nj <= j + 1; ++nj)
                        {
                            int n_index = nk + dimensional_nodes_[dk] * (nj + dimensional_nodes_[dj] * i);
                            Node &node = nodes_[n_index];
                            node.neighboring_surfaces.push_back(index);
                        }
                    }
                    
                    index += 1;
                }
            }
        }
        break;
    }
    }
    Assert(surfaces_.size() == number_of_background_surfaces_);
    
    // Get KD tree
    vector<vector<double> > kd_positions(number_of_background_nodes_, vector<double>(dimension_));
    for (int i = 0; i < number_of_background_nodes_; ++i)
    {
        kd_positions[i] = nodes_[i].position;
    }
    node_tree_ = make_shared<KD_Tree>(dimension_,
                                        number_of_background_nodes_,
                                        kd_positions);

    // Get maximum interval
    max_interval_ = *max_element(intervals_.begin(), intervals_.end());
}

void Weight_Function_Integration::Mesh::
initialize_connectivity()
{
    int number_of_points = wfi_.number_of_points_;

    // Get weight function connectivity
    for (int i = 0; i < number_of_points; ++i)
    {
        // Get weight function data
        shared_ptr<Weight_Function> weight = wfi_.weights_[i];
        double radius = get_inclusive_radius(weight->radius());
        vector<double> const position = weight->position();
        
        // Find nodes that intersect with the weight function
        vector<int> intersecting_nodes;
        vector<double> distances;
        int number_of_intersecting_nodes
            = node_tree_->radius_search(radius,
                                          position,
                                          intersecting_nodes,
                                          distances);
        
        // Add weight indices to cells and surfaces
        for (int j = 0; j < number_of_intersecting_nodes; ++j)
        {
            Node &node = nodes_[intersecting_nodes[j]];
            
            for (int c_index : node.neighboring_cells)
            {
                cells_[c_index].weight_indices.push_back(i);
            }
            
            for (int s_index : node.neighboring_surfaces)
            {
                surfaces_[s_index].weight_indices.push_back(i);
            }
        }
    }
    
    // Get basis function connectivity
    if (!wfi_.identical_basis_functions_)
    {
        for (int i = 0; i < number_of_points; ++i)
        {
            // Get basis function data
            shared_ptr<Basis_Function> basis = wfi_.bases_[i];
            double radius = get_inclusive_radius(basis->radius());
            vector<double> const position = basis->position();

            // Find nodes that intersect with the basis function
            vector<int> intersecting_nodes;
            vector<double> distances;
            int number_of_intersecting_nodes
                = node_tree_->radius_search(radius,
                                            position,
                                            intersecting_nodes,
                                            distances);
        
            // Add basis indices to cells and surfaces
            for (int j = 0; j < number_of_intersecting_nodes; ++j)
            {
                Node &node = nodes_[intersecting_nodes[j]];
            
                for (int c_index : node.neighboring_cells)
                {
                    cells_[c_index].basis_indices.push_back(i);
                }

                for (int s_index : node.neighboring_surfaces)
                {
                    surfaces_[s_index].basis_indices.push_back(i);
                }
            }
        }
    }
    
    // Remove duplicate volume indices
    for (int i = 0; i < number_of_background_cells_; ++i)
    {
        Cell &cell = cells_[i];

        // Remove duplicate weight indices
        sort(cell.weight_indices.begin(), cell.weight_indices.end());
        cell.weight_indices.erase(unique(cell.weight_indices.begin(), cell.weight_indices.end()), cell.weight_indices.end());
        cell.number_of_weight_functions = cell.weight_indices.size();

        // Remove duplicate basis indices
        if (wfi_.identical_basis_functions_)
        {
            cell.basis_indices = cell.weight_indices;
            cell.number_of_basis_functions = cell.number_of_weight_functions;
        }
        else
        {
            sort(cell.basis_indices.begin(), cell.basis_indices.end());
            cell.basis_indices.erase(unique(cell.basis_indices.begin(), cell.basis_indices.end()), cell.basis_indices.end());
            cell.number_of_basis_functions = cell.basis_indices.size();
        }
    }
    
    // Remove duplicate surface indices
    for (int i = 0; i < number_of_background_surfaces_; ++i)
    {
        Surface &surface = surfaces_[i];

        // Remove duplicate weight indices
        sort(surface.weight_indices.begin(), surface.weight_indices.end());
        surface.weight_indices.erase(unique(surface.weight_indices.begin(), surface.weight_indices.end()), surface.weight_indices.end());
        surface.number_of_weight_functions = surface.weight_indices.size();

        // Remove duplicate basis indices
        if (wfi_.identical_basis_functions_)
        {
            surface.basis_indices = surface.weight_indices;
            surface.number_of_basis_functions = surface.number_of_weight_functions;
        }
        else
        {
            sort(surface.basis_indices.begin(), surface.basis_indices.end());
            surface.basis_indices.erase(unique(surface.basis_indices.begin(), surface.basis_indices.end()), surface.basis_indices.end());
            surface.number_of_basis_functions = surface.basis_indices.size();
        }
    }
}

double Weight_Function_Integration::Mesh::
get_inclusive_radius(double radius) const
{
    // Move radius outward to account for surfaces
    switch (dimension_)
    {
    case 1:
        return radius;
    case 2:
        return sqrt(radius * radius + 0.25 * max_interval_ * max_interval_);
    case 3:
        return sqrt(radius * radius + 0.5 * max_interval_ * max_interval_);
    default:
        AssertMsg(false, "dimension cannot exceed 3");
        return -1.;
    }
}

