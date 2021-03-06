#ifndef Eigen_Fixed_Dense_Solver_hh
#define Eigen_Fixed_Dense_Solver_hh

#include <vector>

#include <Eigen/Dense>

#include "Dense_Solver.hh"

template<int const size_, class Scalar>
class Eigen_Fixed_Dense_Solver : public Dense_Solver<Scalar>
{
public:
    
    // Matrices and vectors
    typedef Eigen::Matrix<Scalar, size_, Eigen::Dynamic> EMatrixD;
    typedef Eigen::Matrix<Scalar, size_, size_, Eigen::RowMajor> EMatrixR;
    typedef Eigen::Matrix<Scalar, size_, 1> EVector;
    
    // Mapped types
    typedef Eigen::Map<EMatrixD> EMMatrixD;
    typedef Eigen::Map<EMatrixR> EMMatrixR;
    typedef Eigen::Map<EVector> EMVector;
    
    // LU decomposition
    typedef Eigen::FullPivLU<EMatrixR> ELU;
    
    // Constructor
    Eigen_Fixed_Dense_Solver():
        initialized_(false)
    {
    }

    // Rank of matrix
    virtual int size() const override
    {
        return size_;
    }
    
    // Check whether data has been initialized
    virtual bool initialized() const override
    {
        return initialized_;
    }
    
    // Set matrix and perform decomposition
    virtual void initialize(std::vector<Scalar> &a_data) override
    {
        Check(a_data.size() == size_ * size_);
        
        a_ = EMMatrixR(&a_data[0]);
        lu_ = a_.fullPivLu();
        initialized_ = true;
    }
    
    // Solve problem Ax=b using temporary data (no initialization needed)
    virtual void solve(std::vector<Scalar> &a_data,
                       std::vector<Scalar> &b_data,
                       std::vector<Scalar> &x_data) override
    {
        Check(a_data.size() == size_ * size_);
        Check(b_data.size() == size_);
        Check(x_data.size() == size_);
        
        EMMatrixR a(&a_data[0]);
        EMVector b(&b_data[0]);
        EMVector x(&x_data[0]);
        
        x = a.fullPivLu().solve(b);
    }
    
    // Apply to one vector
    virtual void solve(std::vector<Scalar> &b_data,
                       std::vector<Scalar> &x_data) override
    {
        Assert(initialized_);
        Check(b_data.size() == size_);
        Check(x_data.size() == size_);
        
        EMVector b(&b_data[0]);
        EMVector x(&x_data[0]);
        
        x = lu_.solve(b);
    }

    // Apply to multiple vectors (possibly a matrix)
    virtual void multi_solve(int number_of_vectors,
                             std::vector<Scalar> &b_data,
                             std::vector<Scalar> &x_data) override
    {
        Assert(initialized_);
        Assert(b_data.size() == size_ * number_of_vectors);
        Assert(x_data.size() == size_ * number_of_vectors);

        EMMatrixD b(&b_data[0], size_, number_of_vectors);
        EMMatrixD x(&x_data[0], size_, number_of_vectors);

        x = lu_.solve(b);
    }

    // Get inverse
    virtual void inverse(std::vector<Scalar> &ainv_data) override
    {
        Assert(initialized_);
        Check(ainv_data.size() == size_ * size_);
        
        EMMatrixR ainv(&ainv_data[0]);
        
        ainv = lu_.inverse();
    }
    virtual void inverse(std::vector<Scalar> &a_data,
                         std::vector<Scalar> &ainv_data) override
    {
        Check(a_data.size() == size_ * size_);
        Check(ainv_data.size() == size_ * size_);

        EMMatrixR a(&a_data[0]);
        EMMatrixR ainv(&ainv_data[0]);
        
        ainv = a.fullPivLu().inverse();
    }

    // Get determinant
    virtual Scalar determinant() override
    {
        Assert(initialized_);
        
        return lu_.determinant();
    }

private:

    bool initialized_;
    EMatrixR a_;
    ELU lu_;
};

#endif
