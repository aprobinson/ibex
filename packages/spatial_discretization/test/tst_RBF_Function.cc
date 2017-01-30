#include <cmath>
#include <iostream>
#include <memory>

#include "Cartesian_Distance.hh"
#include "Check_Equality.hh"
#include "Distance.hh"
#include "Multiquadric_RBF.hh"
#include "RBF.hh"
#include "RBF_Function.hh"
#include "String_Functions.hh"

using namespace std;

namespace ce = Check_Equality;
namespace sf = String_Functions;

int test_rbf_function(shared_ptr<RBF_Function> rbf_function,
                      string test_case,
                      int dimension,
                      double const expected_value,
                      vector<double> const &expected_grad,
                      vector<double> const &expected_double_grad,
                      vector<double> const &r)
{
    int checksum = 0;

    double tol = 1e-15;
    
    // Check value
    
    double value = rbf_function->value(r);
    
    if (!ce::approx(value, expected_value, tol))
    {
        cout << "value failed for ";
        cout << test_case;
        cout << endl;
        cout << "\texpected: ";
        cout << expected_value;
        cout << "\tcalculated: ";
        cout << value;
        cout << endl;
        checksum += 1;
    }

    // Check first derivatives
    
    for (int d = 0; d < dimension; ++d)
    {
        double d_value = rbf_function->d_value(d,
                                               r);
            
        if (!ce::approx(d_value, expected_grad[d], tol))
        {
            cout << "d_value in dimension ";
            cout << d;
            cout << " failed for ";
            cout << test_case;
            cout << endl;
            cout << "\texpected: ";
            cout << expected_grad[d];
            cout << "\tcalculated: ";
            cout << d_value;
            cout << endl;
            checksum += 1;
        }
    }
        
    vector<double> grad = rbf_function->gradient_value(r);
        
    if (!ce::approx(grad, expected_grad, tol))
    {
        string eg, gr;
        sf::vector_to_string(eg, expected_grad);
        sf::vector_to_string(gr, grad);
            
        cout << "grad value failed for ";
        cout << test_case;
        cout << endl;
        cout << "\texpected: ";
        cout << eg;
        cout << endl;
        cout << "\tcalculated: ";
        cout << gr;
        cout << endl;
        checksum += 1;
    }

    // Check second derivatives
    
    for (int d = 0; d < dimension; ++d)
    {
        double dd_value = rbf_function->dd_value(d,
                                                 r);
            
        if (!ce::approx(dd_value, expected_double_grad[d + dimension * d], tol))
        {
            cout << "dd_value in dimension ";
            cout << d;
            cout << " failed for ";
            cout << test_case;
            cout << endl;
            cout << "\texpected: ";
            cout << expected_double_grad[d + dimension * d];
            cout << "\tcalculated: ";
            cout << dd_value;
            cout << endl;
            checksum += 1;
        }
    }

    double expected_laplacian = 0;
        
    for (int d = 0; d < dimension; ++d)
    {
        expected_laplacian += expected_double_grad[d + dimension * d];
    }

    double laplacian = rbf_function->laplacian_value(r);
        
    if (!ce::approx(laplacian, expected_laplacian, tol))
    {
        cout << "laplacian failed for ";
        cout << test_case;
        cout << endl;
        cout << "\texpected: ";
        cout << expected_laplacian;
        cout << "\tcalculated: ";
        cout << laplacian;
        cout << endl;
        checksum += 1;
    }
    
    return checksum;
}

int main()
{
    int checksum = 0;

    // Test 1
    
    {
        int const dimension = 2;
        
        vector<double> const r = {4,
                                  -3};
        vector<double> const r0 = {-2,
                                   7};
        vector<double> const direction = {1 / sqrt(3.),
                                          sqrt(2.) / sqrt(3.)};
        
        double const shape = 2.0;

        shared_ptr<RBF> rbf
            = make_shared<Multiquadric_RBF>();
        shared_ptr<Distance> distance
            = make_shared<Cartesian_Distance>(dimension);

        // Regular RBF
        
        {
            string test_case = "standard rbf";
            
            shared_ptr<RBF_Function> rbf_function
                = make_shared<RBF_Function>(shape,
                                            r0,
                                            rbf,
                                            distance);
            
            double const expected_value = sqrt(545.);
            vector<double> const expected_grad = {24. / sqrt(545.),
                                                  -8. * sqrt(5. / 109.)};
            vector<double> const expected_double_grad = {1604. / (545. * sqrt(545.)),
                                                         192. / (109. * sqrt(545.)),
                                                         192. / (109. * sqrt(545.)),
                                                         116. / (109. * sqrt(545.))};
            
            checksum += test_rbf_function(rbf_function,
                                          test_case,
                                          dimension,
                                          expected_value,
                                          expected_grad,
                                          expected_double_grad,
                                          r);
        }
    }
    
    return checksum;
}
