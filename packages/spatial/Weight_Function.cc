#include "Weight_Function.hh"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "Angular_Discretization.hh"
#include "Basis_Function.hh"
#include "Boundary_Source.hh"
#include "Cartesian_Plane.hh"
#include "Conversion.hh"
#include "Cross_Section.hh"
#include "Dimensional_Moments.hh"
#include "Energy_Discretization.hh"
#include "Material.hh"
#include "Meshless_Function.hh"
#include "Quadrature_Rule.hh"
#include "Solid_Geometry.hh"
#include "XML_Node.hh"
#include "Weak_Spatial_Discretization.hh"

using namespace std;
namespace qr = Quadrature_Rule;

Weight_Function::
Weight_Function(int index,
                int dimension,
                shared_ptr<Weight_Function_Options> options,
                shared_ptr<Weak_Spatial_Discretization_Options> weak_options,
                shared_ptr<Meshless_Function> meshless_function,
                vector<shared_ptr<Basis_Function> > basis_functions,
                shared_ptr<Dimensional_Moments> dimensional_moments,
                shared_ptr<Solid_Geometry> solid_geometry,
                vector<shared_ptr<Cartesian_Plane> > boundary_surfaces):
    index_(index),
    dimension_(dimension),
    position_(meshless_function->position()),
    number_of_basis_functions_(basis_functions.size()),
    number_of_boundary_surfaces_(boundary_surfaces.size()),
    radius_(meshless_function->radius()),
    options_(make_shared<Weight_Function_Options>(*options)), // copy to edit
    weak_options_(weak_options),
    meshless_function_(meshless_function),
    basis_functions_(basis_functions),
    solid_geometry_(solid_geometry),
    dimensional_moments_(dimensional_moments),
    boundary_surfaces_(boundary_surfaces)
{
    set_options_and_limits();
    calculate_values();
    if (!weak_options_->external_integral_calculation
        && weak_options_->perform_integration)
    {
        calculate_integrals();
        calculate_material();
        calculate_boundary_source();
        check_class_invariants();
    }
}

Weight_Function::
Weight_Function(int index,
                int dimension,
                shared_ptr<Weight_Function_Options> options,
                shared_ptr<Weak_Spatial_Discretization_Options> weak_options,
                shared_ptr<Meshless_Function> meshless_function,
                vector<shared_ptr<Basis_Function> > basis_functions,
                shared_ptr<Dimensional_Moments> dimensional_moments,
                shared_ptr<Solid_Geometry> solid_geometry,
                vector<shared_ptr<Cartesian_Plane> > boundary_surfaces,
                shared_ptr<Material> material,
                Integrals const &integrals):
    index_(index),
    dimension_(dimension),
    position_(meshless_function->position()),
    material_(material),
    number_of_basis_functions_(basis_functions.size()),
    number_of_boundary_surfaces_(boundary_surfaces.size()),
    radius_(meshless_function->radius()),
    options_(options),
    weak_options_(weak_options),
    meshless_function_(meshless_function),
    basis_functions_(basis_functions),
    solid_geometry_(solid_geometry),
    dimensional_moments_(dimensional_moments),
    boundary_surfaces_(boundary_surfaces),
    integrals_(integrals)
{
    AssertMsg(false, "not tested");
    set_options_and_limits();
    calculate_values();
    calculate_boundary_source();
    check_class_invariants();
}

void Weight_Function::
set_options_and_limits()
{
    point_type_ = (number_of_boundary_surfaces_ > 0 ?
                   Weight_Function::Point_Type::BOUNDARY :
                   Weight_Function::Point_Type::INTERNAL);

    // Check whether weak parameter options are finalized
    if (!weak_options_->input_finalized)
    {
        weak_options_->finalize_input();
    }
    
    // Calculate boundary limits
    double lim = 0.5 * numeric_limits<double>::max();
    min_boundary_limits_.assign(dimension_, -lim);
    max_boundary_limits_.assign(dimension_, lim);
    local_surface_indices_.assign(dimension_ * 2, Errors::DOES_NOT_EXIST);
    for (int i = 0; i < number_of_boundary_surfaces_; ++i)
    {
        shared_ptr<Cartesian_Plane> surface = boundary_surfaces_[i];
        int dim_sur = surface->surface_dimension();
        double pos_sur = surface->position();
        double n_sur = surface->normal();
        
        if (n_sur < 0)
        {
            min_boundary_limits_[dim_sur] = max(min_boundary_limits_[dim_sur], pos_sur);
            local_surface_indices_[0 + 2 * dim_sur] = i;
        }
        else
        {
            max_boundary_limits_[dim_sur] = min(max_boundary_limits_[dim_sur], pos_sur);
            local_surface_indices_[1 + 2 * dim_sur] = i;
        }
    }

    // Check that Galerkin option is set
    Assert(weak_options_->identical_basis_functions != Weak_Spatial_Discretization_Options::Identical_Basis_Functions::AUTO);
    
    // Set SUPG options
    if (weak_options_->include_supg)
    {
        // Scale tau appropriately to boundary
        if (number_of_boundary_surfaces_ > 0)
        {
            // Find closest surface
            shared_ptr<Cartesian_Plane> closest_surface;
            double min_distance = 0.5 * numeric_limits<double>::max();
            for (shared_ptr<Cartesian_Plane> surface : boundary_surfaces_)
            {
                int dim_sur = surface->surface_dimension();
                double pos_sur = surface->position();
                double dist = std::abs(pos_sur - position_[dim_sur]);
                if (dist < min_distance)
                {
                    closest_surface = surface;
                    min_distance = dist;
                }
            }
            Assert(closest_surface >= 0);

            // Get boundary position
            vector<double> b_position(position_);
            b_position[closest_surface->surface_dimension()]
                = closest_surface->position();

            // Get ratio of old to new tau
            double ratio = 1;
            switch (weak_options_->tau_scaling)
            {
            case Weak_Spatial_Discretization_Options::Tau_Scaling::NONE:
            case Weak_Spatial_Discretization_Options::Tau_Scaling::CONSTANT:
                ratio = 1;
                break;
            case Weak_Spatial_Discretization_Options::Tau_Scaling::ABSOLUTE:
                ratio = 0;
                break;
            case Weak_Spatial_Discretization_Options::Tau_Scaling::LINEAR:
            {
                double dist = std::abs(closest_surface->position() - position_[closest_surface->surface_dimension()]);
                ratio = dist / radius_;
                break;
            }
            case Weak_Spatial_Discretization_Options::Tau_Scaling::FUNCTIONAL:
            {
                double val_rad = meshless_function_->value(b_position);
                double val_center = meshless_function_->value(position_);
                ratio = val_rad / val_center;
                break;
            }
            }

            // Check to see if ratio has out-of-bounds values
            if (ratio > 1)
            {
                ratio = 1;
            }
            else if (ratio < 0)
            {
                ratio = 0;
            }

            // Set new tau
            options_->tau_const *= ratio;
        }

        switch (weak_options_->tau_scaling)
        {
        case Weak_Spatial_Discretization_Options::Tau_Scaling::CONSTANT:
            options_->tau = options_->tau_const;
            break;
        default:
            options_->tau = options_->tau_const / meshless_function_->shape();
            break;
        }
    }

    // Get basis function indices
    basis_function_indices_.resize(number_of_basis_functions_);
    basis_global_indices_.clear();
    for (int i = 0; i < number_of_basis_functions_; ++i)
    {
        int index = basis_functions_[i]->index();
        basis_function_indices_[i] = index;
        basis_global_indices_[index] = i;
    }
}

bool Weight_Function::
get_full_quadrature(vector<vector<double> > &integration_ordinates,
                    vector<double> &integration_weights) const
{
    switch (dimension_)
    {
    case 1:
        return get_full_quadrature_1d(integration_ordinates,
                                      integration_weights);
    case 2:
        return get_full_quadrature_2d(integration_ordinates,
                                      integration_weights);
    default:
        return false;
    }
}

bool Weight_Function::
get_full_quadrature_1d(vector<vector<double> > &integration_ordinates,
                       vector<double> &integration_weights) const
{
    // Get min and max of weight
    double position = position_[0];
    double x1 = position - radius_;
    double x2 = position + radius_;

    // Compare to boundaries
    x1 = max(x1, min_boundary_limits_[0]);
    x2 = min(x2, max_boundary_limits_[0]);
    
    // Get quadrature
    vector<double> ordinates_x;
    bool success =  qr::cartesian_1d(qr::Quadrature_Type::GAUSS_LEGENDRE,
                                     weak_options_->integration_ordinates,
                                     x1,
                                     x2,
                                     ordinates_x,
                                     integration_weights);
    qr::convert_to_position_1d(ordinates_x,
                               integration_ordinates);
    return success;
}
    
bool Weight_Function::
get_full_quadrature_2d(vector<vector<double> > &integration_ordinates,
                       vector<double> &integration_weights) const
{
    // Return standard cylindrical quadrature if no boundary surfaces
    bool success = false;
    vector<double> ordinates_x;
    vector<double> ordinates_y;
    if (number_of_boundary_surfaces_ == 0)
    {
        success = qr::cylindrical_2d(qr::Quadrature_Type::GAUSS_LEGENDRE,
                                     qr::Quadrature_Type::GAUSS_LEGENDRE,
                                     weak_options_->integration_ordinates,
                                     weak_options_->integration_ordinates,
                                     position_[0],
                                     position_[1],
                                     0.,
                                     radius_,
                                     0.,
                                     2. * M_PI,
                                     ordinates_x,
                                     ordinates_y,
                                     integration_weights);
    }
    else
    {
        success = qr::cartesian_bounded_cylindrical_2d(qr::Quadrature_Type::GAUSS_LEGENDRE,
                                                       qr::Quadrature_Type::GAUSS_LEGENDRE,
                                                       weak_options_->integration_ordinates,
                                                       weak_options_->integration_ordinates,
                                                       position_[0],
                                                       position_[1],
                                                       radius_,
                                                       min_boundary_limits_[0],
                                                       max_boundary_limits_[0],
                                                       min_boundary_limits_[1],
                                                       max_boundary_limits_[1],
                                                       ordinates_x,
                                                       ordinates_y,
                                                       integration_weights);
    }

    qr::convert_to_position_2d(ordinates_x,
                               ordinates_y,
                               integration_ordinates);
    
    return success;
}

bool Weight_Function::
get_basis_quadrature(int i,
                     vector<vector<double> > &integration_ordinates,
                     vector<double> &integration_weights) const
{
    switch (dimension_)
    {
    case 1:
        return get_basis_quadrature_1d(i,
                                       integration_ordinates,
                                       integration_weights);
    case 2:
        return get_basis_quadrature_2d(i,
                                       integration_ordinates,
                                       integration_weights);
    default:
        return false;
    }
}

bool Weight_Function::
get_basis_quadrature_1d(int i,
                        vector<vector<double> > &integration_ordinates,
                        vector<double> &integration_weights) const
{
    // Get basis and weight positional information
    shared_ptr<Basis_Function> basis = basis_function(i);
    double basis_position = basis->position()[0];
    double weight_position = position_[0];
    double basis_radius = basis->radius();

    // Get starting/ending positions for basis and weight
    double x1 = max(weight_position - radius_, basis_position - basis_radius);
    double x2 = min(weight_position + radius_, basis_position + basis_radius);

    // Apply boundary surfaces
    if (min_boundary_limits_[0] > x1)
    {
        x1 = min_boundary_limits_[0];
    }
    if (max_boundary_limits_[0] < x2)
    {
        x2 = max_boundary_limits_[0];
    }
    
    // Get quadrature
    vector<double> ordinates_x;
    bool success = qr::cartesian_1d(qr::Quadrature_Type::GAUSS_LEGENDRE,
                                    weak_options_->integration_ordinates,
                                    x1,
                                    x2,
                                    ordinates_x,
                                    integration_weights);
    qr::convert_to_position_1d(ordinates_x,
                               integration_ordinates);
    return success;
}
    
bool Weight_Function::
get_basis_quadrature_2d(int i,
                        vector<vector<double> > &integration_ordinates,
                        vector<double> &integration_weights) const
{
    // Get basis information
    shared_ptr<Basis_Function> basis = basis_function(i);
    vector<double> basis_position = basis->position();

    // If either weight or basis does not intersect surface,
    // return standard double_cylindrical quadrature
    bool success;
    vector<double> ordinates_x;
    vector<double> ordinates_y;
    if (number_of_boundary_surfaces_ == 0
        || basis->number_of_boundary_surfaces() == 0)
    {
        success = qr::double_cylindrical_2d(qr::Quadrature_Type::GAUSS_LEGENDRE,
                                            qr::Quadrature_Type::GAUSS_LEGENDRE,
                                            weak_options_->integration_ordinates,
                                            weak_options_->integration_ordinates,
                                            position_[0],
                                            position_[1],
                                            radius_,
                                            basis_position[0],
                                            basis_position[1],
                                            basis->radius(),
                                            ordinates_x,
                                            ordinates_y,
                                            integration_weights);
    }
    else
    {
        success = qr::cartesian_bounded_double_cylindrical_2d(qr::Quadrature_Type::GAUSS_LEGENDRE,
                                                              qr::Quadrature_Type::GAUSS_LEGENDRE,
                                                              weak_options_->integration_ordinates,
                                                              weak_options_->integration_ordinates,
                                                              position_[0],
                                                              position_[1],
                                                              radius_,
                                                              basis_position[0],
                                                              basis_position[1],
                                                              basis->radius(),
                                                              min_boundary_limits_[0],
                                                              max_boundary_limits_[0],
                                                              min_boundary_limits_[1],
                                                              max_boundary_limits_[1],
                                                              ordinates_x,
                                                              ordinates_y,
                                                              integration_weights);
    }
     
    if (!success)
    {
        cout << "weight: " << index_ << endl;
        cout << "basis: " << basis->index() << endl;
    }
     
    qr::convert_to_position_2d(ordinates_x,
                               ordinates_y,
                               integration_ordinates);

    return success;
}

bool Weight_Function::
get_full_surface_quadrature(int s,
                            vector<vector<double> > &integration_ordinates,
                            vector<double> &integration_weights) const
{
    switch (dimension_)
    {
    case 1:
        integration_ordinates.assign(1, vector<double>({boundary_surfaces_[s]->position()}));
        integration_weights.assign(1, 1.);
        return true;
    case 2:
        return get_full_surface_quadrature_2d(s,
                                              integration_ordinates,
                                              integration_weights);
    default:
        return false;
    }
}

bool Weight_Function::
get_full_surface_quadrature_2d(int s,
                               vector<vector<double> > &integration_ordinates,
                               vector<double> &integration_weights) const
{
    // Get surface information
    shared_ptr<Cartesian_Plane> surface = boundary_surfaces_[s];
    int dim_sur = surface->surface_dimension();
    int dim_other = (dim_sur == 0 ? 1 : 0);
    double pos_sur = surface->position();

    // Calculate bounds on surface integral
    double dist = std::abs(pos_sur - position_[dim_sur]);
    double l = sqrt(radius_ * radius_ - dist * dist);
    double smin = position_[dim_other] - l;
    double smax = position_[dim_other] + l;

    // Check for boundaries
    smin = max(smin, min_boundary_limits_[dim_other]);
    smax = min(smax, max_boundary_limits_[dim_other]);
     
    // Get ordinates for integration dimension
    vector<double> ordinates_main;
    bool success = qr::cartesian_1d(qr::Quadrature_Type::GAUSS_LEGENDRE,
                                    weak_options_->integration_ordinates,
                                    smin,
                                    smax,
                                    ordinates_main,
                                    integration_weights);

    // Get ordinates for constant dimension
    vector<double> ordinates_other(ordinates_main.size(), pos_sur);

    switch (dim_sur)
    {
    case 0:
        qr::convert_to_position_2d(ordinates_other,
                                   ordinates_main,
                                   integration_ordinates);
        return success;
    case 1:
        qr::convert_to_position_2d(ordinates_main,
                                   ordinates_other,
                                   integration_ordinates);
        return success;
    default:
        return false;
    }
}

bool Weight_Function::
get_basis_surface_quadrature(int i,
                             int s,
                             vector<vector<double> > &integration_ordinates,
                             vector<double> &integration_weights) const
{
    switch (dimension_)
    {
    case 1:
        integration_ordinates.assign(1, vector<double>({boundary_surfaces_[s]->position()}));
        integration_weights.assign(1, 1.);
        return true;
    case 2:
        return get_basis_surface_quadrature_2d(i,
                                               s,
                                               integration_ordinates,
                                               integration_weights);
    default:
        return false;
    }
}

bool Weight_Function::
get_basis_surface_quadrature_2d(int i,
                                int s,
                                vector<vector<double> > &integration_ordinates,
                                vector<double> &integration_weights) const
{
    // Get surface information
    shared_ptr<Cartesian_Plane> surface = boundary_surfaces_[s];
    int dim_sur = surface->surface_dimension();
    int dim_other = (dim_sur == 0 ? 1 : 0);
    double pos_sur = surface->position();

    // Check basis function boundaries
    vector<double> basis_position = basis_functions_[i]->position();
    double basis_radius = basis_functions_[i]->radius();
    int number_of_basis_boundary_surfaces = basis_functions_[i]->number_of_boundary_surfaces();
    double distb = std::abs(pos_sur - basis_position[dim_sur]);

    // If basis function does not intersect, return empty quadrature
    if (number_of_basis_boundary_surfaces == 0 || distb > basis_radius)
    {
        integration_ordinates.resize(0);
        integration_weights.resize(0);

        return true;
    }

    double lb = sqrt(basis_radius * basis_radius - distb * distb);
    double sbmin = basis_position[dim_other] - lb;
    double sbmax = basis_position[dim_other] + lb;

    // Calculate bounds on surface integral
    double dist = std::abs(pos_sur - position_[dim_sur]);
    double l = sqrt(radius_ * radius_ - dist * dist);
    double smin = max(position_[dim_other] - l, sbmin);
    double smax = min(position_[dim_other] + l, sbmax);

    // Check for boundaries
    smin = max(smin, min_boundary_limits_[dim_other]);
    smax = min(smax, max_boundary_limits_[dim_other]);

    if (smin > smax)
    {
        integration_ordinates.resize(0);
        integration_weights.resize(0);

        return true;
    }

    vector<double> ordinates_main;
    bool success = qr::cartesian_1d(qr::Quadrature_Type::GAUSS_LEGENDRE,
                                    weak_options_->integration_ordinates,
                                    smin,
                                    smax,
                                    ordinates_main,
                                    integration_weights);
    
    vector<double> ordinates_other(ordinates_main.size(), pos_sur);

    switch (dim_sur)
    {
    case 0:
        qr::convert_to_position_2d(ordinates_other,
                                   ordinates_main,
                                   integration_ordinates);
        return success;
    case 1:
        qr::convert_to_position_2d(ordinates_main,
                                   ordinates_other,
                                   integration_ordinates);
        return success;
    default:
        return false;
    }
}

void Weight_Function::
calculate_values()
{
    // Calculate value of basis functions at weight center
    values_.v_b.resize(number_of_basis_functions_);
    values_.v_db.resize(number_of_basis_functions_ * dimension_);
    for (int i = 0; i < number_of_basis_functions_; ++i)
    {
        shared_ptr<Meshless_Function> basis = basis_functions_[i]->function();
        double b = basis->value(position_);
        vector<double> db = basis->gradient_value(position_);

        values_.v_b[i] = b;

        for (int d = 0; d < dimension_; ++d)
        {
            values_.v_db[d + dimension_ * i] = db[d];
        }
    }
}

void Weight_Function::
calculate_integrals()
{
    // Perform basis/weight integrals
    integrals_.is_b_w.assign(number_of_boundary_surfaces_ * number_of_basis_functions_, 0.);
    integrals_.iv_b_w.assign(number_of_basis_functions_, 0.);
    integrals_.iv_b_dw.assign(number_of_basis_functions_ * dimension_, 0.);
    integrals_.iv_db_w.assign(number_of_basis_functions_ * dimension_, 0.);
    integrals_.iv_db_dw.assign(number_of_basis_functions_ * dimension_ * dimension_, 0.);

    shared_ptr<Meshless_Function> weight = meshless_function_;

    for (int i = 0; i < number_of_basis_functions_; ++i)
    {
        shared_ptr<Meshless_Function> basis = basis_functions_[i]->function();

        // Surface integrals
        for (int s = 0; s < number_of_boundary_surfaces_; ++s)
        {
            // Get quadrature
            vector<vector<double> > integration_ordinates;
            vector<double> integration_weights;
            Assert(get_basis_surface_quadrature(i,
                                                s,
                                                integration_ordinates,
                                                integration_weights));
            int number_of_integration_ordinates = integration_ordinates.size();
             
            for (int o = 0; o < number_of_integration_ordinates; ++o)
            {
                vector<double> position = integration_ordinates[o];
                double b = basis->value(position);
                double w = weight->value(position);

                integrals_.is_b_w[s + number_of_boundary_surfaces_ * i] += integration_weights[o] * b * w;
            }
        }

        // Volume integrals
        vector<vector<double> > integration_ordinates;
        vector<double> integration_weights;

        Assert(get_basis_quadrature(i,
                                    integration_ordinates,
                                    integration_weights));
        int number_of_integration_ordinates = integration_ordinates.size();

        for (int o = 0; o < number_of_integration_ordinates; ++o)
        {
            vector<double> position = integration_ordinates[o];
            double b = basis->value(position);
            double w = weight->value(position);
            vector<double> db = basis->gradient_value(position);
            vector<double> dw = weight->gradient_value(position);

            integrals_.iv_b_w[i] += integration_weights[o] * b * w;

            for (int d1 = 0; d1 < dimension_; ++d1)
            {
                int k1 = d1 + dimension_ * i;
                integrals_.iv_b_dw[k1] += integration_weights[o] * b * dw[d1];
                integrals_.iv_db_w[k1] += integration_weights[o] * db[d1] * w;
                 
                for (int d2 = 0; d2 < dimension_; ++d2)
                {
                    int k2 = d1 + dimension_ * (d2 + dimension_ * i);
                    integrals_.iv_db_dw[k2] += integration_weights[o] * db[d1] * dw[d2];
                }
            }
        }
    }

    // Perform weight integrals
    integrals_.is_w.assign(number_of_boundary_surfaces_, 0.);
    integrals_.iv_w.assign(1, 0.);
    integrals_.iv_dw.assign(dimension_, 0.);
     
    // Surface integrals
    for (int s = 0; s < number_of_boundary_surfaces_; ++s)
    {
        vector<vector<double> > integration_ordinates;
        vector<double> integration_weights;
        Assert(get_full_surface_quadrature(s,
                                           integration_ordinates,
                                           integration_weights));
        int number_of_integration_ordinates = integration_weights.size();
        shared_ptr<Meshless_Function> weight = meshless_function_;
        for (int i = 0; i < number_of_integration_ordinates; ++i)
        {
            vector<double> position = integration_ordinates[i];
            double w = weight->value(position);

            integrals_.is_w[s] += w * integration_weights[i];
        }
         
    }

    // Volume integrals
    vector<vector<double> > integration_ordinates;
    vector<double> integration_weights;
    Assert(get_full_quadrature(integration_ordinates,
                               integration_weights));
    int number_of_integration_ordinates = integration_weights.size();
    for (int i = 0; i < number_of_integration_ordinates; ++i)
    {
        vector<double> position = integration_ordinates[i];
        double w = meshless_function_->value(position);
        integrals_.iv_w[0] += w * integration_weights[i];
        vector<double> dw = meshless_function_->gradient_value(position);
        for (int d = 0; d < dimension_; ++d)
        {
            integrals_.iv_dw[d] += dw[d] * integration_weights[i];
        }
    }
}

void Weight_Function::
calculate_material()
{
    // Make sure options for material weighting are compatible
    // switch (weak_options_->weighting)
    // {
    // case Weak_Spatial_Discretization_Options::Weighting::POINT:
    //     options_.total = Weak_Spatial_Discretization_Options::Total::ISOTROPIC;
    //     break; // POINT
    // case Weak_Spatial_Discretization_Options::Weighting::FLAT:
    //     options_.total = Weak_Spatial_Discretization_Options::Total::ISOTROPIC;
    //     break; // WEIGHT
    // case Weak_Spatial_Discretization_Options::Weighting::FLUX:
    //     AssertMsg(false, "not yet implemented");
    //     break; // FLUX
    // }

    if (weak_options_->include_supg)
    {
        switch(weak_options_->weighting)
        {
        case Weak_Spatial_Discretization_Options::Weighting::POINT:
        {
            return calculate_supg_point_material();
        } // POINT
        case Weak_Spatial_Discretization_Options::Weighting::FLAT:
        {
            return calculate_supg_weight_material();
        } // WEIGHT
        default:
        {
            AssertMsg(false, "Weight_Function flux and full weighting not yet implemented");
            return;
        } // FLUX
        } // Weighting
    }
    else
    {
        switch(weak_options_->weighting)
        {
        case Weak_Spatial_Discretization_Options::Weighting::POINT:
        {
            return calculate_standard_point_material();
        } // POINT
        case Weak_Spatial_Discretization_Options::Weighting::FLAT:
        {
            return calculate_standard_weight_material();
        } // WEIGHT
        default:
        {
            AssertMsg(false, "Weight_Function flux and full weighting not yet implemented");
            return;
        } // FLUX
        } // Weighting
    }
}

void Weight_Function::
calculate_standard_point_material()
{
    // Get material for angular and energy discretization
    shared_ptr<Material> test_material = solid_geometry_->material(position_);
    shared_ptr<Angular_Discretization> angular_discretization = test_material->angular_discretization();
    shared_ptr<Energy_Discretization> energy_discretization = test_material->energy_discretization();

    // Get material data
    int number_of_groups = energy_discretization->number_of_groups();
    int number_of_scattering_moments = angular_discretization->number_of_scattering_moments();
    int number_of_moments = angular_discretization->number_of_moments();

    // Get the weighted internal source
    vector<double> const internal_source_temp = test_material->internal_source()->data();
    int internal_size = internal_source_temp.size();
    vector<double> internal_source_v(internal_size, 0.);
    for (int g = 0; g < internal_size; ++g)
    {
        internal_source_v[g] = integrals_.iv_w[0] * internal_source_temp[g];
    }
    
    shared_ptr<Cross_Section> internal_source
        = make_shared<Cross_Section>(test_material->internal_source()->dependencies(),
                                     angular_discretization,
                                     energy_discretization,
                                     internal_source_v);

    material_
        = make_shared<Material>(index_,
                                angular_discretization,
                                energy_discretization,
                                test_material->sigma_t(),
                                test_material->sigma_s(),
                                test_material->nu(),
                                test_material->sigma_f(),
                                test_material->chi(),
                                internal_source);
}

void Weight_Function::
calculate_standard_weight_material()
{
    // Get ordinates
    vector<vector<double> > integration_ordinates;
    vector<double> integration_weights;
    Assert(get_full_quadrature(integration_ordinates,
                               integration_weights));
    int number_of_integration_ordinates = integration_ordinates.size();

    // Get material for angular and energy discretization
    shared_ptr<Material> test_material = solid_geometry_->material(position_);
    shared_ptr<Angular_Discretization> angular_discretization = test_material->angular_discretization();
    shared_ptr<Energy_Discretization> energy_discretization = test_material->energy_discretization();

    // Get material data
    int number_of_groups = energy_discretization->number_of_groups();
    int number_of_scattering_moments = angular_discretization->number_of_scattering_moments();
    int number_of_moments = angular_discretization->number_of_moments();

    vector<double> sigma_t_v(number_of_groups, 0.);
    vector<double> sigma_s_v(number_of_groups * number_of_groups * number_of_scattering_moments, 0.);
    vector<double> nu_v(1, 0);
    vector<double> sigma_f_v(number_of_groups * number_of_groups, 0.);
    vector<double> chi_v(1, 0);
    vector<double> internal_source_v(number_of_groups * number_of_moments, 0.);

    // Calculate numerator and denominator separately
    for (int i = 0; i < number_of_integration_ordinates; ++i)
    {
        vector<double> position = integration_ordinates[i];
        shared_ptr<Material> material = solid_geometry_->material(position);
        vector<double> const sigma_t_temp = material->sigma_t()->data();
        vector<double> const sigma_s_temp = material->sigma_s()->data();
        vector<double> const nu_temp = material->nu()->data();
        vector<double> const sigma_f_temp = material->sigma_f()->data();
        vector<double> const chi_temp = material->chi()->data();
        vector<double> const internal_source_temp = material->internal_source()->data();
        double w = meshless_function_->value(position);

        for (int g = 0; g < number_of_groups; ++g)
        {
            sigma_t_v[g] += w * sigma_t_temp[g] * integration_weights[i];
            for (int m = 0; m < number_of_moments; ++m)
            {
                int k = g + number_of_groups * m;
                internal_source_v[k] += w * internal_source_temp[k] * integration_weights[i];
            }
            
            for (int g2 = 0; g2 < number_of_groups; ++g2)
            {
                // Fission from g2 to g
                int k1 = g2 + number_of_groups * g;
                sigma_f_v[k1] += w * chi_temp[g] * nu_temp[g2] * sigma_f_temp[g2] * integration_weights[i];
                
                for (int m = 0; m < number_of_scattering_moments; ++m)
                {
                    // Scattering from g2 to g
                    int k2 = g2 + number_of_groups * (g + number_of_groups * m);
                    sigma_s_v[k2] += w * sigma_s_temp[k2] * integration_weights[i];
                }
            } // groups2
        } // groups
    } // integration ordinates

    // Divide numerator by denominator for cross sections
    if (weak_options_->normalized)
    {
        for (int g = 0; g < number_of_groups; ++g)
        {
            sigma_t_v[g] /= integrals_.iv_w[0];
            
            for (int g2 = 0; g2 < number_of_groups; ++g2)
            {
                int k1 = g2 + number_of_groups * g;
                sigma_f_v[k1] /= integrals_.iv_w[0];
                
                for (int m = 0; m < number_of_scattering_moments; ++m)
                {
                    int k2 = g2 + number_of_groups * (g + number_of_groups * m);
                    sigma_s_v[k2] /= integrals_.iv_w[0];
                }
            }
        }
    }
     
    // Create cross sections
    Cross_Section::Dependencies none_none;
    Cross_Section::Dependencies none_group;
    none_group.energy = Cross_Section::Dependencies::Energy::GROUP;
    Cross_Section::Dependencies none_group2;
    none_group2.energy = Cross_Section::Dependencies::Energy::GROUP_TO_GROUP;
    Cross_Section::Dependencies moment_group;
    moment_group.angular = Cross_Section::Dependencies::Angular::MOMENTS;
    moment_group.energy = Cross_Section::Dependencies::Energy::GROUP;
    Cross_Section::Dependencies scattering_group2;
    scattering_group2.angular = Cross_Section::Dependencies::Angular::SCATTERING_MOMENTS;
    scattering_group2.energy = Cross_Section::Dependencies::Energy::GROUP_TO_GROUP;
     
    shared_ptr<Cross_Section> sigma_t
        = make_shared<Cross_Section>(none_group,
                                     angular_discretization,
                                     energy_discretization,
                                     sigma_t_v);
    shared_ptr<Cross_Section> sigma_s
        = make_shared<Cross_Section>(scattering_group2,
                                     angular_discretization,
                                     energy_discretization,
                                     sigma_s_v);
    shared_ptr<Cross_Section> nu
        = make_shared<Cross_Section>(none_none,
                                     angular_discretization,
                                     energy_discretization,
                                     nu_v);
    shared_ptr<Cross_Section> sigma_f
        = make_shared<Cross_Section>(none_group2,
                                     angular_discretization,
                                     energy_discretization,
                                     sigma_f_v);
    shared_ptr<Cross_Section> chi
        = make_shared<Cross_Section>(none_none,
                                     angular_discretization,
                                     energy_discretization,
                                     chi_v);
    shared_ptr<Cross_Section> internal_source
        = make_shared<Cross_Section>(moment_group,
                                     angular_discretization,
                                     energy_discretization,
                                     internal_source_v);

    // Create material
    material_ = make_shared<Material>(index_,
                                      angular_discretization,
                                      energy_discretization,
                                      sigma_t,
                                      sigma_s,
                                      nu,
                                      sigma_f,
                                      chi,
                                      internal_source);

}

void Weight_Function::
calculate_supg_point_material()
{
    // Get material for angular and energy discretization
    shared_ptr<Material> test_material = solid_geometry_->material(position_);
    shared_ptr<Angular_Discretization> angular_discretization = test_material->angular_discretization();
    shared_ptr<Energy_Discretization> energy_discretization = test_material->energy_discretization();

    // Get material data
    int number_of_groups = energy_discretization->number_of_groups();
    int number_of_scattering_moments = angular_discretization->number_of_scattering_moments();
    int number_of_moments = angular_discretization->number_of_moments();
    int dimensionp1 = dimension_ + 1;
    vector<double> const sigma_t_temp = test_material->sigma_t()->data();
    vector<double> const sigma_s_temp = test_material->sigma_s()->data();
    vector<double> const nu_temp = test_material->nu()->data();
    vector<double> const sigma_f_temp = test_material->sigma_f()->data();
    vector<double> const chi_temp = test_material->chi()->data();
    vector<double> const internal_source_temp = test_material->internal_source()->data();

    // Get weighted cross sections
    vector<double> sigma_t_v(dimensionp1 * number_of_groups, 0);
    vector<double> sigma_s_v(dimensionp1 * number_of_groups * number_of_groups * number_of_scattering_moments, 0);
    vector<double> nu_v(1, 0);
    vector<double> sigma_f_v(dimensionp1 * number_of_groups * number_of_groups, 0);
    vector<double> chi_v(1, 0);;
    vector<double> internal_source_v(dimensionp1 * number_of_groups * number_of_moments, 0);
    
    // Get function weight values
    vector<double> w(dimension_ + 1, 0);
    w[0] = integrals_.iv_w[0];
    for (int j = 0; j < dimension_; ++j)
    {
        w[j+1] = integrals_.iv_dw[j];
    }
    
    for (int j = 0; j < dimension_ + 1; ++j)
    {
        for (int g = 0; g < number_of_groups; ++g)
        {
            int k1 = j + dimensionp1 * g;
            sigma_t_v[k1] = w[j] * sigma_t_temp[g];
            
            for (int m = 0; m < number_of_moments; ++m)
            {
                int k = j + dimensionp1 * (g + number_of_groups * m);
                internal_source_v[k] = w[j] * internal_source_temp[g + number_of_groups * m];
            }
            
            for (int g2 = 0; g2 < number_of_groups; ++g2)
            {
                // Fission from g2 to g
                int k2 = j + dimensionp1 * (g2 + number_of_groups * g);
                sigma_f_v[k2] = w[j] * chi_temp[g] * nu_temp[g2] * sigma_f_temp[g2];
                
                for (int m = 0; m < number_of_scattering_moments; ++m)
                {
                    // Scattering from g2 to g
                    int k3 = j + dimensionp1 * (g2 + number_of_groups * (g + number_of_groups * m));
                    int k4 = g2 + number_of_groups * (g + number_of_groups * m);
                    sigma_s_v[k3] = w[j] * sigma_s_temp[k4];
                }
            }
        }
    }
    
    // Create cross sections
    Cross_Section::Dependencies none_none;
    Cross_Section::Dependencies none_group;
    none_group.energy = Cross_Section::Dependencies::Energy::GROUP;
    none_group.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
    Cross_Section::Dependencies none_group2;
    none_group2.energy = Cross_Section::Dependencies::Energy::GROUP_TO_GROUP;
    none_group2.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
    Cross_Section::Dependencies moment_group;
    moment_group.angular = Cross_Section::Dependencies::Angular::MOMENTS;
    moment_group.energy = Cross_Section::Dependencies::Energy::GROUP;
    moment_group.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
    Cross_Section::Dependencies scattering_group2;
    scattering_group2.angular = Cross_Section::Dependencies::Angular::SCATTERING_MOMENTS;
    scattering_group2.energy = Cross_Section::Dependencies::Energy::GROUP_TO_GROUP;
    scattering_group2.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
    shared_ptr<Cross_Section> sigma_t
        = make_shared<Cross_Section>(none_group,
                                     angular_discretization,
                                     energy_discretization,
                                     sigma_t_v);
    shared_ptr<Cross_Section> sigma_s
        = make_shared<Cross_Section>(scattering_group2,
                                     angular_discretization,
                                     energy_discretization,
                                     sigma_s_v);
    shared_ptr<Cross_Section> nu
        = make_shared<Cross_Section>(none_none,
                                     angular_discretization,
                                     energy_discretization,
                                     nu_v);
    shared_ptr<Cross_Section> sigma_f
        = make_shared<Cross_Section>(none_group2,
                                     angular_discretization,
                                     energy_discretization,
                                     sigma_f_v);
    shared_ptr<Cross_Section> chi
        = make_shared<Cross_Section>(none_none,
                                     angular_discretization,
                                     energy_discretization,
                                     chi_v);
    shared_ptr<Cross_Section> internal_source
        = make_shared<Cross_Section>(moment_group,
                                     angular_discretization,
                                     energy_discretization,
                                     internal_source_v);
    
    // Create material
    material_ = make_shared<Material>(index_,
                                      angular_discretization,
                                      energy_discretization,
                                      sigma_t,
                                      sigma_s,
                                      nu,
                                      sigma_f,
                                      chi,
                                      internal_source);
}

void Weight_Function::
calculate_supg_weight_material()
{
    // Get ordinates
    vector<vector<double> > integration_ordinates;
    vector<double> integration_weights;
    Assert(get_full_quadrature(integration_ordinates,
                               integration_weights));
    int number_of_integration_ordinates = integration_ordinates.size();

    // Get material for angular and energy discretization
    shared_ptr<Material> test_material = solid_geometry_->material(position_);
    shared_ptr<Angular_Discretization> angular_discretization = test_material->angular_discretization();
    shared_ptr<Energy_Discretization> energy_discretization = test_material->energy_discretization();

    // Get material data
    int number_of_groups = energy_discretization->number_of_groups();
    int number_of_scattering_moments = angular_discretization->number_of_scattering_moments();
    int number_of_moments = angular_discretization->number_of_moments();
    int dimensionp1 = dimension_ + 1;

    vector<double> sigma_t_v(dimensionp1 * number_of_groups, 0);
    vector<double> sigma_s_v(dimensionp1 * number_of_groups * number_of_groups * number_of_scattering_moments, 0);
    vector<double> nu_v(1, 0);
    vector<double> sigma_f_v(dimensionp1 * number_of_groups * number_of_groups, 0);
    vector<double> chi_v(1, 0);;
    vector<double> internal_source_v(dimensionp1 * number_of_groups * number_of_moments, 0);

    // Calculate numerator and denominator separately
    for (int i = 0; i < number_of_integration_ordinates; ++i)
    {
        vector<double> position = integration_ordinates[i];
        shared_ptr<Material> material = solid_geometry_->material(position);
        vector<double> const sigma_t_temp = material->sigma_t()->data();
        vector<double> const sigma_s_temp = material->sigma_s()->data();
        vector<double> const nu_temp = material->nu()->data();
        vector<double> const sigma_f_temp = material->sigma_f()->data();
        vector<double> const chi_temp = material->chi()->data();
        vector<double> const internal_source_temp = material->internal_source()->data();

        // Get function weight values
        double w1 = meshless_function_->value(position);
        vector<double> w2 = meshless_function_->gradient_value(position);
        vector<double> w(dimension_ + 1, 0);
        w[0] = w1;
        for (int j = 0; j < dimension_; ++j)
        {
            w[j+1] = w2[j];
        }
         
        for (int j = 0; j < dimension_ + 1; ++j)
        {
            for (int g = 0; g < number_of_groups; ++g)
            {
                int k1 = j + dimensionp1 * g;
                sigma_t_v[k1] += w[j] * sigma_t_temp[g] * integration_weights[i];
                for (int m = 0; m < number_of_moments; ++m)
                {
                    int k = j + dimensionp1 * (g + number_of_groups * m);
                    internal_source_v[k] += w[j] * internal_source_temp[g + number_of_groups * m] * integration_weights[i];
                }

                for (int g2 = 0; g2 < number_of_groups; ++g2)
                {
                    // Fission from g2 to g
                    int k2 = j + dimensionp1 * (g2 + number_of_groups * g);
                    sigma_f_v[k2] += w[j] * chi_temp[g] * nu_temp[g2] * sigma_f_temp[g2] * integration_weights[i];
                    
                    for (int m = 0; m < number_of_scattering_moments; ++m)
                    {
                        // Scattering from g2 to g
                        int k3 = j + dimensionp1 * (g2 + number_of_groups * (g + number_of_groups * m));
                        int k4 = g2 + number_of_groups * (g + number_of_groups * m);
                        sigma_s_v[k3] += w[j] * sigma_s_temp[k4] * integration_weights[i];
                    }
                }
            }
        }
    }

    // Divide numerator by denominator for cross sections
    if (weak_options_->normalized)
    {
        for (int j = 0; j < dimension_ + 1; ++j)
        {    
            for (int g = 0; g < number_of_groups; ++g)
            {
                double den = j == 0 ? integrals_.iv_w[0] : integrals_.iv_dw[j-1];
             
                int k1 = j + dimensionp1 * g;
                sigma_t_v[k1] /= den;
                nu_v[k1] /= den;
                sigma_f_v[k1] /= den;
                chi_v[k1] /= den;
             
                for (int g2 = 0; g2 < number_of_groups; ++g2)
                {
                    int k2 = j + dimensionp1 * (g2 + number_of_groups * g);
                    sigma_f_v[k2] /= den;
                    
                    for (int m = 0; m < number_of_scattering_moments; ++m)
                    {
                        int k3 = j + dimensionp1 * (g2 + number_of_groups * (g + number_of_groups * m));
                        sigma_s_v[k2] /= den;
                    }
                }
            }
        }
    }
    // Create cross sections
    Cross_Section::Dependencies none_none;
    Cross_Section::Dependencies none_group;
    none_group.energy = Cross_Section::Dependencies::Energy::GROUP;
    none_group.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
    Cross_Section::Dependencies none_group2;
    none_group2.energy = Cross_Section::Dependencies::Energy::GROUP_TO_GROUP;
    none_group2.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
    Cross_Section::Dependencies moment_group;
    moment_group.angular = Cross_Section::Dependencies::Angular::MOMENTS;
    moment_group.energy = Cross_Section::Dependencies::Energy::GROUP;
    moment_group.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;
    Cross_Section::Dependencies scattering_group2;
    scattering_group2.angular = Cross_Section::Dependencies::Angular::SCATTERING_MOMENTS;
    scattering_group2.energy = Cross_Section::Dependencies::Energy::GROUP_TO_GROUP;
    scattering_group2.dimensional = Cross_Section::Dependencies::Dimensional::SUPG;

    shared_ptr<Cross_Section> sigma_t
        = make_shared<Cross_Section>(none_group,
                                     angular_discretization,
                                     energy_discretization,
                                     sigma_t_v);
    shared_ptr<Cross_Section> sigma_s
        = make_shared<Cross_Section>(scattering_group2,
                                     angular_discretization,
                                     energy_discretization,
                                     sigma_s_v);
    shared_ptr<Cross_Section> nu
        = make_shared<Cross_Section>(none_none,
                                     angular_discretization,
                                     energy_discretization,
                                     nu_v);
    shared_ptr<Cross_Section> sigma_f
        = make_shared<Cross_Section>(none_group2,
                                     angular_discretization,
                                     energy_discretization,
                                     sigma_f_v);
    shared_ptr<Cross_Section> chi
        = make_shared<Cross_Section>(none_none,
                                     angular_discretization,
                                     energy_discretization,
                                     chi_v);
    shared_ptr<Cross_Section> internal_source
        = make_shared<Cross_Section>(moment_group,
                                     angular_discretization,
                                     energy_discretization,
                                     internal_source_v);
    
    // Create material
    material_ = make_shared<Material>(index_,
                                      angular_discretization,
                                      energy_discretization,
                                      sigma_t,
                                      sigma_s,
                                      nu,
                                      sigma_f,
                                      chi,
                                      internal_source);
}

void Weight_Function::
calculate_boundary_source()
{
    boundary_sources_.resize(number_of_boundary_surfaces_);
    for (int s = 0; s < number_of_boundary_surfaces_; ++s)
    {
        shared_ptr<Cartesian_Plane> surface = boundary_surfaces_[s];
        shared_ptr<Boundary_Source> source = surface->boundary_source();
        int source_size = source->size();
        vector<double> const old_data = source->data();
        vector<double> new_data(source_size);
        
        for (int i = 0; i < source_size; ++i)
        {
            new_data[i] = old_data[i] * integrals_.is_w[s];
        }
        
        Boundary_Source::Dependencies bs_dependencies = source->dependencies();
        bs_dependencies.angular = Boundary_Source::Dependencies::Angular::ORDINATES;
        shared_ptr<Boundary_Source> new_source
            = make_shared<Boundary_Source>(s + number_of_boundary_surfaces_ * index_,
                                           bs_dependencies,
                                           source->angular_discretization(),
                                           source->energy_discretization(),
                                           new_data,
                                           source->alpha());
        boundary_sources_[s] = new_source;
    }
}

void Weight_Function::
check_class_invariants() const
{
    // Check that all boundary surfaces actually intersect
    for (shared_ptr<Cartesian_Plane> surface : boundary_surfaces_)
    {
        double position = surface->position();
        int surface_dim = surface->surface_dimension();
        
        Assert(std::abs(position_[surface_dim] - position) <= radius_);
    }
    
    // Check sizes
    Assert(dimension_ == solid_geometry_->dimension());
    Assert(position_.size() == dimension_);
    Assert(material_);
    Assert(meshless_function_);
    Assert(basis_functions_.size() == number_of_basis_functions_);
    Assert(solid_geometry_);
    Assert(boundary_surfaces_.size() == number_of_boundary_surfaces_);
    Assert(boundary_sources_.size() == number_of_boundary_surfaces_);
    Assert(min_boundary_limits_.size() == dimension_);
    Assert(max_boundary_limits_.size() == dimension_);
    Assert(values_.v_b.size() == number_of_basis_functions_);
    Assert(values_.v_db.size() == number_of_basis_functions_ * dimension_);
    Assert(integrals_.is_w.size() == number_of_boundary_surfaces_);
    Assert(integrals_.is_b_w.size() == number_of_boundary_surfaces_ * number_of_basis_functions_);
    Assert(integrals_.iv_w.size() == 1);
    Assert(integrals_.iv_dw.size() == dimension_);
    Assert(integrals_.iv_b_w.size() == number_of_basis_functions_);
    Assert(integrals_.iv_b_dw.size() == number_of_basis_functions_ * dimension_);
    Assert(integrals_.iv_db_w.size() == number_of_basis_functions_ * dimension_);
    Assert(integrals_.iv_db_dw.size() == number_of_basis_functions_ * dimension_ * dimension_);
}

void Weight_Function::
output(XML_Node output_node) const
{
    output_node.set_attribute(index_, "index");
    output_node.set_attribute(point_type_conversion()->convert(point_type_), "point_type");
    output_node.set_child_value(dimension_, "dimension");
    output_node.set_child_vector(position_, "position");
    if (options_->output_material)
    {
        material_->output(output_node.append_child("material"));
    }
    output_node.set_child_value(number_of_basis_functions_, "number_of_basis_functions");
    output_node.set_child_value(radius_, "radius");
    output_node.set_child_value(options_->tau_const, "tau_const");
    output_node.set_child_value(options_->tau, "tau");
    meshless_function_->output(output_node.append_child("function"));
    
    vector<int> basis_functions(number_of_basis_functions_);
    for (int i = 0; i < number_of_basis_functions_; ++i)
    {
        basis_functions[i] = basis_functions_[i]->index();
    }
    output_node.set_child_vector(basis_functions, "basis_functions");
    
    vector<int> boundary_surfaces(number_of_boundary_surfaces_);
    for (int i = 0; i < number_of_boundary_surfaces_; ++i)
    {
        boundary_surfaces[i] = boundary_surfaces_[i]->index();
    }
    output_node.set_child_vector(boundary_surfaces, "boundary_surfaces");
    
    output_node.set_child_vector(min_boundary_limits_, "min_boundary_limits");
    output_node.set_child_vector(max_boundary_limits_, "max_boundary_limits");

    if (options_->output_integrals)
    {
        output_node.set_child_vector(integrals_.is_w, "is_w", "surface");
        output_node.set_child_vector(integrals_.is_b_w, "is_b_w", "surface-basis");
        output_node.set_child_vector(integrals_.iv_w, "iv_w");
        output_node.set_child_vector(integrals_.iv_dw, "iv_dw", "dimension");
        output_node.set_child_vector(integrals_.iv_b_w, "iv_b_w", "basis");
        output_node.set_child_vector(integrals_.iv_b_dw, "iv_b_dw", "dimension-basis");
        output_node.set_child_vector(integrals_.iv_db_w, "iv_db_w", "dimension-basis");
        output_node.set_child_vector(integrals_.iv_db_dw, "iv_db_dw", "dimension-dimension-basis");
    }
}

void Weight_Function::
set_integrals(Weight_Function::Integrals const &integrals,
              shared_ptr<Material> material,
              vector<shared_ptr<Boundary_Source> > boundary_sources)
{
    // Set integral data
    integrals_ = integrals;
    material_ = material;
    boundary_sources_ = boundary_sources;
    
    // Complete initialization
    check_class_invariants();
}

int Weight_Function::
local_basis_index(int global_index) const
{
    if (basis_global_indices_.count(global_index) > 0)
    {
        return basis_global_indices_.at(global_index);
    }
    else
    {
        return Errors::DOES_NOT_EXIST;
    }
}

int Weight_Function::
local_surface_index(int surface_dimension,
                    double normal) const
{
    if (normal < 0)
    {
        return local_surface_indices_[2 * surface_dimension];
    }
    else
    {
        return local_surface_indices_[1 + 2 * surface_dimension];
    }
}

