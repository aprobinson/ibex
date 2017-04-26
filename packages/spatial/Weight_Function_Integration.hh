#ifndef Weight_Function_Integration_hh
#define Weight_Function_Integration_hh

#include "Weight_Function.hh"

class KD_Tree;

class Weight_Function_Integration
{
public:
    
    class Mesh
    {
    public:

        class Cell
        {
            // Spatial information
            std::vector<std::vector<double> > limits;

            // Connectivity
            std::vector<int> neighboring_nodes;
            
            // Intersections
            std::vector<int> basis_indices;
            std::vector<int> weight_indices;
        };
        
        class Node
        {
            // Spatial information
            std::vector<double> position;

            // Connectivity
            std::vector<int> neighboring_cells;
        };
        
        Mesh(Weight_Function_Integration const &wfi,
             int dimension,
             std::vector<std::vector<double> > limits,
             std::vector<int> num_intervals);
        
    private:

        void initialize_mesh();
        void initialize_connectivity();
        void get_inclusive_radius(double radius) const;

        Weight_Function_Integration const &wfi_;
        int number_of_local_nodes_;
        int number_of_global_nodes_;
        int number_of_global_cells_;
        int dimension_;
        double max_interval_;
        std::vector<std::vector<double> > limits_;
        std::vector<int> dimensional_cells_;
        std::vector<int> dimensional_nodes_;
        std::vector<double> intervals_;
        std::shared_ptr<KD_Tree> kd_tree_;
        vector<Cell> cells_;
        vector<Node> nodes_;
    };
    
    Weight_Function_Integration(Weight_Function::Options options,
                                int number_of_points,
                                std::vector<std::shared_ptr<Basis_Function> > const &bases,
                                std::vector<std::shared_ptr<Weight_Function> > const &weights,
                                std::shared_ptr<Solid_Geometry> solid_geometry,
                                std::vector<std::vector<double> > limits,
                                std::vector<int> num_intervals);
    
private:
    
    Weight_Function::Options options_;
    std::vector<std::shared_ptr<Basis_Function> > bases_
    std::vector<std::shared_ptr<Weight_Function> > weights_;
    std::shared_ptr<Solid_Geometry> solid_;
    std::shared_ptr<Mesh> mesh_;
    std::vector<std::vector<double> > limits_;
};

#endif
