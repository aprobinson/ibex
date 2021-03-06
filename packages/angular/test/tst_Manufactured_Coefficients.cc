#include <cmath>
#include <iomanip>
#include <iostream>

#include "Angular_Discretization.hh"
#include "Angular_Discretization_Factory.hh"
#include "Check_Equality.hh"

namespace ce = Check_Equality;

using namespace std;

int check_coefficients(int dimension,
                       int number_of_scattering_moments,
                       int angular_rule)
{
    int checksum = 0;
    
    // Get angular discretization
    Angular_Discretization_Factory factory;
    shared_ptr<Angular_Discretization> angular
        = factory.get_angular_discretization(dimension,
                                             number_of_scattering_moments,
                                             angular_rule);
    int number_of_moments = angular->number_of_moments();
    
    // Get coefficients
    vector<int> size;
    vector<vector<int> > indices;
    vector<vector<double> > coefficients;
    angular->manufactured_coefficients(size,
                                       indices,
                                       coefficients);

    // Print
    vector<int> l_vals = angular->harmonic_degrees();
    vector<int> m_vals = angular->harmonic_orders();
    for (int o1 = 0; o1 < number_of_moments; ++o1)
    {
        int l1 = l_vals[o1];
        int m1 = m_vals[o1];

        int num_indices = size[o1];
        cout << o1 << endl;
        for (int n = 0; n < num_indices; ++n)
        {
            int o2 = indices[o1][n];
            int l2 = l_vals[o2];
            int m2 = m_vals[o2];

            int w = 4;
            cout << setw(w) << l1;
            cout << setw(w) << m1;
            cout << setw(w) << l2;
            cout << setw(w) << m2;
            for (int d = 0; d < dimension; ++d)
            {
                cout << setw(12) << coefficients[o1][d + dimension * n];
            }
            cout << endl;
        }
    }
    cout << endl;

    return checksum;
}

int main()
{
    int checksum = 0;

    int dimension;
    int angular_rule;
    int scattering_order;
    {
        dimension = 1;
        angular_rule = 16;
        scattering_order = 4;
        checksum += check_coefficients(dimension,
                                       scattering_order,
                                       angular_rule);
    }
    {
        dimension = 3;
        angular_rule = 4;
        scattering_order = 3;

        checksum += check_coefficients(dimension,
                                       scattering_order,
                                       angular_rule);
    }

    return checksum;
}
