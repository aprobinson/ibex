#ifndef VERA_Solid_Geometry_hh
#define VERA_Solid_Geometry_hh

#include <functional>
#include <memory>
#include <vector>

#include "Check.hh"
#include "Solid_Geometry.hh"
#include "XML_Node.hh"

typedef std::function<double(std::vector<double> const &)> VERA_Temperature;

class Angular_Discretization;
class Boundary_Source;
class Cartesian_Plane;
class Energy_Discretization;
class Material;
class Material_Factory;
class Surface;

class VERA_Solid_Geometry : public Solid_Geometry
{
public:

    VERA_Solid_Geometry(bool include_ifba,
                        bool include_crack,
                        std::shared_ptr<VERA_Temperature> temperature,
                        std::shared_ptr<Angular_Discretization> angular,
                        std::shared_ptr<Energy_Discretization> energy,
                        std::vector<std::shared_ptr<Material> > materials,
                        std::shared_ptr<Boundary_Source> boundary_source);

    virtual int dimension() const override;
    virtual int find_region(std::vector<double> const &position) const override
    {
        AssertMsg(false, "not implemented");
        return Geometry_Errors::NO_REGION;
    }
    virtual int find_surface(std::vector<double> const &position) const override
    {
        AssertMsg(false, "not implemented");
        return Geometry_Errors::NO_SURFACE;
    }
    virtual int next_intersection(std::vector<double> const &initial_position,
                                  std::vector<double> const &initial_direction,
                                  int &final_region,
                                  double &distance,
                                  std::vector<double> &final_position) const override
    {
        AssertMsg(false, "not implemented");
        return Geometry_Errors::NO_SURFACE;
    }
    virtual int next_boundary(std::vector<double> const &initial_position,
                              std::vector<double> const &initial_direction,
                              int &boundary_region,
                              double &distance,
                              std::vector<double> &final_position) const override
    {
        AssertMsg(false, "not implemented");
        return Geometry_Errors::NO_SURFACE;
    }        
    virtual void optical_distance(std::vector<double> const &initial_position,
                                  std::vector<double> const &final_position,
                                  std::vector<double> &optical_distance) const override
    {
        AssertMsg(false, "not implemented");
    }
    virtual std::shared_ptr<Material> material(std::vector<double> const &position) const override;
    virtual std::shared_ptr<Boundary_Source> boundary_source(std::vector<double> const &position) const override;
    virtual void check_class_invariants() const override;
    virtual void output(XML_Node output_node) const override
    {
        output_node.set_attribute("VERA_Solid_Geometry", "type");
    }
    
    std::vector<std::shared_ptr<Cartesian_Plane> > cartesian_boundary_surfaces() const
    {
        return boundary_surfaces_;
    }
    
private:

    enum Material_Type
    {
        NONE = -1,
        FUEL = 0,
        IFBA = 1,
        GAP = 2,
        CLAD = 3,
        MOD = 4
    };
    enum Problem_Type
    {
        V1B = 0,
        V1E = 1
    };
    enum Temperature_Type
    {
        K600 = 0,
        K1200 = 1
    };
    
    double radial_distance2(std::vector<double> const &position) const;
    std::shared_ptr<Material> get_material_by_index(Material_Type mat_type,
                                                    Problem_Type prob_type,
                                                    Temperature_Type temp_type) const;
    std::vector<double> weighted_cross_section(double temperature,
                                               std::vector<double> const &xs600,
                                               std::vector<double> const &xs1200) const;
    std::shared_ptr<Material> weighted_material(double temperature,
                                                std::shared_ptr<Material> mat600,
                                                std::shared_ptr<Material> mat1200) const;
    
    // Data
    bool include_ifba_;
    bool include_crack_;
    int num_crack_surfaces_;
    std::vector<std::shared_ptr<Surface>> crack_surfaces_;
    std::shared_ptr<VERA_Temperature> temperature_;
    std::shared_ptr<Angular_Discretization> angular_;
    std::shared_ptr<Energy_Discretization> energy_;
    std::shared_ptr<Material_Factory> material_factory_;
    std::vector<double> material_radii2_;
    std::vector<std::shared_ptr<Cartesian_Plane> > boundary_surfaces_;

    // Cross sections
    int number_of_material_types_;
    int number_of_problem_types_;
    int number_of_temperature_types_;
    std::vector<std::shared_ptr<Material> > materials_;
    std::shared_ptr<Boundary_Source> boundary_source_;
};

#endif
