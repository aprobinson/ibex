#include "Weight_Function_Integration.hh"

#include <algorithm>
#include <cmath>

using std::sqrt;
using std::shared_ptr;
using std::vector;

Weight_Function_Integration::
Weight_Function_Integration(Weight_Function::Options options,
                            int number_of_points,
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
    
    mesh_ = make_shared<Mesh>(*this,
                              solid->dimension(),
                              limits,
                              dimensional_cells);
}

Weight_Function_Integration::Mesh
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

void Weight_Function_Integration::Mesh
initialize_mesh()
{
    vector<vector<double> > positions;
    
    // Check sizes
    Assert(dimensional_cells_.size() == dimension);
    Assert(limits_.size() == dimension);
    
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
    intervals_.resize(dimension);
    for (int d = 0; d < dimension; ++d)
    {
        intervals_[d] = (limits_[d][1] - limits_[d][0]) / static_cast<double>(dimensional_cells_[d]);
    }
    

    // Initialize nodes
    nodes_.resize(number_of_background_nodes_);
    switch (dimension)
    {
    case 1:
    {
        int d_i = 0;
        for (int i = 0; i < dimensional_nodes_[0]; ++i)
        {
            double index = i;
            Node &node = nodes_[i];
            node.position = {limits_[d_i][0] + intervals_[d_i] * i};
        }
        break;
    }
    case 2:
    {
        int d_i = 0;
        int d_j = 1;
        for (int i = 0; i < dimensional_nodes_[0]; ++i)
        {
            for (int j = 0; j < dimensional_nodes_[1]; ++i)
            {
                int index = j + dimensional_nodes_[1] * i;
                Node &node = nodes_[index];
                node.position = {limits_[d_i][0] + intervals_[d_i] * i,
                                 limits_[d_j][0] + intervals_[d_j] * j};
            }
        }
        break;
    }
    case 3:
    {
        int d_i = 0;
        int d_j = 1;
        int d_k = 2;
        for (int i = 0; i < dimensional_nodes_[0]; ++i)
        {
            for (int j = 0; j < dimensional_nodes_[1]; ++i)
            {
                for (int k = 0; k < dimensional_nodes_[2]; ++k)
                {
                    int index = k + dimensional_nodes_[2] * (j + dimensional_nodes_[1] * i);
                    Node &node = nodes_[index];
                    node.position = {limits_[d_i][0] + intervals_[d_i] * i,
                                     limits_[d_j][0] + intervals_[d_j] * j,
                                     limits_[d_k][0] + intervals_[d_k] * k};
                }
            }
            break;
        }
    }
    default:
        AssertMsg(false, "dimension (" << dimension_ << ") not found");
    }

    // Initialize cells
    switch (dimension_)
    {
    case 1:
    {
        break;
    }
    case 2:
    {
        int d_i = 0;
        int d_j = 1;
        for (int i = 0; i < dimensional_cells_[0]; ++i)
        {
            for (int j = 0; j < dimensional_cells_[1]; ++j)
            {
                int index = j + dimensional_cells_[1] * i;
                vector<int> indices = {i, j};
                Cell &cell = cells[index];
                
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
                        int n_index = nj + dimensional_nodes_[1] * ni;
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
        int d_i = 0;
        int d_j = 1;
        for (int i = 0; i < dimensional_cells_[0]; ++i)
        {
            for (int j = 0; j < dimensional_cells_[1]; ++j)
            {
                for (int k = 0; k < dimensional_cells_[2]; ++k)
                {
                    int index = k + dimensional_cells_[2] * (j + dimensional_cells_[1] * i);
                    vector<int> indices = {i, j, k};
                    Cell &cell = cells[index];
                    
                    // Set neighboring nodes and cells
                    for (int ni = i; ni <= i + 1; ++ni)
                    {
                        for (int nj = j; nj <= j + 1; ++nj)
                        {
                            for (int nk = k; nk <= k + 1; ++nk)
                            {
                                int n_index = nk + dimensional_nodes_[2] * (nj + dimensional_nodes_[1] * ni);
                                Node &node = nodes_[n_index];
                                node.neighboring_cells.push_back(index);
                                cell.neighboring_nodes.push_back(n_index);
                            }
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
        break;
    }
    default:
        AssertMsg(false, "dimension (" << dimension_ << ") not found");
    }
    
    // Get KD tree
    vector<vector<double> > kd_positions(number_of_background_nodes_, vector<double>(dimension_));
    for (int i = 0; i < number_of_background_nodes_; ++i)
    {
        kd_positions[i] = nodes_[i].position;
    }
    kd_tree_ = make_shared<KD_Tree>(dimension_,
                                    number_of_background_nodes_,
                                    kd_positions);

    // Get maximum interval
    max_interval_ = *std::max_element(intervals_.begin(), intervals_.end());
}

void Weight_Function_Integration::Mesh
initialize_connectivity()
{
    int number_of_points = wfi_.number_of_points;

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
            = kd_tree_->radius_search(radius,
                                      position,
                                      intersecting_nodes,
                                      distances);
        
        // Add cells for these nodes to the weight indices
        for (int j = 0; j < number_of_intersecting_nodes; ++j)
        {
            Node &node = nodes_[intersecting_nodes[j]];
            
            for (int c_index : node.neighboring_cells)
            {
                cell[c_index].weight_indices.push_back(i);
            }
        }
    }
    
    // Get basis function connectivity
    for (int i = 0; i < number_of_points; ++i)
    {
        // Get basis function data
        shared_ptr<Basis_Function> basis = wfi_.bases_[i];
        double radius = get_inclusive_radius(basis->radius());
        vector<double> const position = basis->position();
        
        // Find nodes that intersect with the weight function
        vector<int> intersecting_nodes;
        vector<double> distances;
        int number_of_intersecting_nodes
            = kd_tree_->radius_search(radius,
                                      position,
                                      intersecting_nodes,
                                      distances);
        
        // Add cells for these nodes to the weight indices
        for (int j = 0; j < number_of_intersecting_nodes; ++j)
        {
            Node &node = nodes_[intersecting_nodes[j]];
            
            for (int c_index : node.neighboring_cells)
            {
                cell[c_index].basis_indices.push_back(i);
            }
        }
    }

    // Remove duplicate basis/weight indices
    for (int i = 0; i < number_of_background_cells_; ++i)
    {
        Cell &cell = cells_[i];
        
        sort(cell.basis_indices.begin(), cell.basis_indices.end());
        cell.basis_indices.erase(unique(cell.basis_indices.begin(), cell.basis_indices.end()), cell.basis_indices.end());
        
        sort(cell.weight_indices.begin(), cell.weight_indices.end());
        cell.weight_indices.erase(unique(cell.weight_indices.begin(), cell.weight_indices.end()), cell.weight_indices.end());
    }
}

void Weight_Function_Integration::Mesh
get_inclusive_radius(double radius) const
{
    // Move radius outward to account for edges
    switch (dimension)
    {
    case 1:
        return radius;
    case 2:
        return sqrt(radius * radius + 0.25 * max_interval_ * max_interval_);
    case 3:
        return sqrt(radius * radius + 0.5 * max_interval_ * max_interval_);
    }
}
