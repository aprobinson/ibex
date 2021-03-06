#ifndef Heat_Transfer_Integration_hh
#define Heat_Transfer_Integration_hh

#include <memory>
#include <vector>

class Heat_Transfer_Data;
class Integration_Mesh;
class Integration_Mesh_Options;
class Integration_Surface;
class Weak_Spatial_Discretization;

struct Heat_Transfer_Integration_Options
{
    enum class Geometry
    {
        CYLINDRICAL_1D,
        CARTESIAN,
        CYLINDRICAL_2D
    };

    Geometry geometry = Geometry::CYLINDRICAL_1D;
};

class Heat_Transfer_Integration
{
public:
    
    Heat_Transfer_Integration(std::shared_ptr<Heat_Transfer_Integration_Options> options,
                              std::shared_ptr<Heat_Transfer_Data> data,
                              std::shared_ptr<Weak_Spatial_Discretization> spatial);

    // Data access
    std::vector<std::vector<double> > &matrix()
    {
        return matrix_;
    }
    std::vector<double> &rhs()
    {
        return rhs_;
    }
    
private:
    
    // Integration methods
    void initialize_integrals();
    void perform_integration();

    // Cylindrical 2D integration methods
    std::shared_ptr<Integration_Surface> get_cylindrical_surface(std::vector<double> limit_t,
                                                                 double radius) const;
    
    // Input data
    std::shared_ptr<Heat_Transfer_Integration_Options> options_;
    std::shared_ptr<Heat_Transfer_Data> data_;
    std::shared_ptr<Weak_Spatial_Discretization> spatial_;
    
    // Utility data
    std::shared_ptr<Integration_Mesh> mesh_;
    
    // Output data
    std::vector<std::vector<double> > matrix_;
    std::vector<double> rhs_;
};

#endif
