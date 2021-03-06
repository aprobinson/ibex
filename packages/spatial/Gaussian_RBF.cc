#include "Gaussian_RBF.hh"

#include <cmath>

Gaussian_RBF::
Gaussian_RBF()
{
}

double Gaussian_RBF::
value(double r) const
{
    return exp(-r * r);
}

double Gaussian_RBF::
d_value(double r) const
{
    return -2 * r * exp(-r * r);
}

double Gaussian_RBF::
dd_value(double r) const
{
    return (-2 + 4 * r * r) * exp(- r * r);
}
