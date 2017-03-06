#include "Scattering.hh"

#include <iostream>
#include <memory>
#include <vector>

#include "Angular_Discretization.hh"
#include "Check.hh"
#include "Cross_Section.hh"
#include "Energy_Discretization.hh"
#include "Material.hh"
#include "Point.hh"
#include "Spatial_Discretization.hh"

using namespace std;

Scattering::
Scattering(shared_ptr<Spatial_Discretization> spatial_discretization,
           shared_ptr<Angular_Discretization> angular_discretization,
           shared_ptr<Energy_Discretization> energy_discretization,
           Options options):
    Scattering_Operator(spatial_discretization,
                        angular_discretization,
                        energy_discretization,
                        options)
{
    check_class_invariants();
}

void Scattering::
check_class_invariants() const
{
    Assert(spatial_discretization_);
    Assert(angular_discretization_);
    Assert(energy_discretization_);

    int number_of_points = spatial_discretization_->number_of_points();
    
    for (int i = 0; i < number_of_points; ++i)
    {
        shared_ptr<Material> material = spatial_discretization_->point(i)->material();
        Cross_Section::Dependencies dep = material->sigma_s()->dependencies();
        Assert(dep.angular == Cross_Section::Dependencies::Angular::SCATTERING_MOMENTS
               || dep.angular == Cross_Section::Dependencies::Angular::MOMENTS);
        Assert(dep.energy == Cross_Section::Dependencies::Energy::GROUP_TO_GROUP);
    }
}

void Scattering::
apply_full(vector<double> &x) const
{
    vector<double> y(x);
    
    int number_of_points = spatial_discretization_->number_of_points();
    int number_of_nodes = spatial_discretization_->number_of_nodes();
    int number_of_groups = energy_discretization_->number_of_groups();
    int number_of_moments = angular_discretization_->number_of_moments();
    int number_of_scattering_moments = angular_discretization_->number_of_scattering_moments();
    int number_of_dimensional_moments = spatial_discretization_->number_of_dimensional_moments();
    int local_number_of_dimensional_moments = (options_.include_dimensional_moments
                                               ? number_of_dimensional_moments
                                               : 1);
    vector<int> const scattering_indices = angular_discretization_->scattering_indices();
    
    for (int i = 0; i < number_of_points; ++i)
    {
        shared_ptr<Cross_Section> sigma_s_cs = spatial_discretization_->point(i)->material()->sigma_s();
        vector<double> const sigma_s = sigma_s_cs->data();
        Cross_Section::Dependencies::Angular angular_dep = sigma_s_cs->dependencies().angular;

        switch (angular_dep)
        {
        case Cross_Section::Dependencies::Angular::SCATTERING_MOMENTS:
        {
            for (int m = 0; m < number_of_moments; ++m)
            {
                int l = scattering_indices[m];

                for (int gt = 0; gt < number_of_groups; ++gt)
                {
                    for (int d = 0; d < local_number_of_dimensional_moments; ++d)
                    {
                        for (int n = 0; n < number_of_nodes; ++n)
                        {
                            double sum = 0;
                            
                            for (int gf = 0; gf < number_of_groups; ++gf)
                            {
                                int k_phi_from = n + number_of_nodes * (d + local_number_of_dimensional_moments * (gf + number_of_groups * (m + number_of_moments * i)));
                                int k_sigma = d + number_of_dimensional_moments * (gf + number_of_groups * (gt + number_of_groups * l));
                                
                                sum += sigma_s[k_sigma] * y[k_phi_from];
                            }
                            
                            int k_phi_to = n + number_of_nodes * (d + local_number_of_dimensional_moments * (gt + number_of_groups * (m + number_of_moments * i)));
                    
                            x[k_phi_to] = sum;
                        }
                    }
                }
            }
            break;
        }
        case Cross_Section::Dependencies::Angular::MOMENTS:
        {
            for (int m = 0; m < number_of_moments; ++m)
            {
                for (int gt = 0; gt < number_of_groups; ++gt)
                {
                    for (int d = 0; d < local_number_of_dimensional_moments; ++d)
                    {
                        for (int n = 0; n < number_of_nodes; ++n)
                        {
                            double sum = 0;
                            
                            for (int gf = 0; gf < number_of_groups; ++gf)
                            {
                                int k_phi_from = n + number_of_nodes * (d + local_number_of_dimensional_moments * (gf + number_of_groups * (m + number_of_moments * i)));
                                int k_sigma = d + number_of_dimensional_moments * (gf + number_of_groups * (gt + number_of_groups * m));
                                
                                sum += sigma_s[k_sigma] * y[k_phi_from];
                            }
                            
                            int k_phi_to = n + number_of_nodes * (d + local_number_of_dimensional_moments * (gt + number_of_groups * (m + number_of_moments * i)));
                    
                            x[k_phi_to] = sum;
                        }
                    }
                }
            }
            break;
        }
        }
    }
}

void Scattering::
apply_coherent(vector<double> &x) const
{
    int number_of_points = spatial_discretization_->number_of_points();
    int number_of_nodes = spatial_discretization_->number_of_nodes();
    int number_of_groups = energy_discretization_->number_of_groups();
    int number_of_moments = angular_discretization_->number_of_moments();
    int number_of_scattering_moments = angular_discretization_->number_of_scattering_moments();
    int number_of_dimensional_moments = spatial_discretization_->number_of_dimensional_moments();
    int local_number_of_dimensional_moments = (options_.include_dimensional_moments
                                               ? number_of_dimensional_moments
                                               : 1);
    vector<int> const scattering_indices = angular_discretization_->scattering_indices();
    
    for (int i = 0; i < number_of_points; ++i)
    {
        shared_ptr<Cross_Section> sigma_s_cs = spatial_discretization_->point(i)->material()->sigma_s();
        vector<double> const sigma_s = sigma_s_cs->data();
        Cross_Section::Dependencies::Angular angular_dep = sigma_s_cs->dependencies().angular;
        
        switch (angular_dep)
        {
        case Cross_Section::Dependencies::Angular::SCATTERING_MOMENTS:
        {
            for (int m = 0; m < number_of_moments; ++m)
            {
                int l = scattering_indices[m];

                for (int g = 0; g < number_of_groups; ++g)
                {
                    for (int d = 0; d < local_number_of_dimensional_moments; ++d)
                    {
                        int k_sigma = d + number_of_dimensional_moments * (g + number_of_groups * (g + number_of_groups * l));
                
                        for (int n = 0; n < number_of_nodes; ++n)
                        {
                            double sum = 0;
                    
                            int k_phi = n + number_of_nodes * (d + local_number_of_dimensional_moments * (g + number_of_groups * (m + number_of_moments * i)));
                    
                            x[k_phi] = sigma_s[k_sigma] * x[k_phi];
                        }
                    }
                }
            }
            break;
        }
        case Cross_Section::Dependencies::Angular::MOMENTS:
        {
            for (int m = 0; m < number_of_moments; ++m)
            {
                for (int g = 0; g < number_of_groups; ++g)
                {
                    for (int d = 0; d < number_of_dimensional_moments; ++d)
                    {
                        int k_sigma = d + number_of_dimensional_moments * (g + number_of_groups * (g + number_of_groups * m));
                        
                        for (int n = 0; n < number_of_nodes; ++n)
                        {
                            double sum = 0;
                    
                            int k_phi = n + number_of_nodes * (d + local_number_of_dimensional_moments * (g + number_of_groups * (m + number_of_moments * i)));
                            
                            x[k_phi] = sigma_s[k_sigma] * x[k_phi];
                        }
                    }
                }
            }
            break;
        }
        }
        
    }
}

