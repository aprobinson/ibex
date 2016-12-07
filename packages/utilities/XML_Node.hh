#ifndef XML_Node_hh
#define XML_Node_hh

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "pugixml.hh"

#include "XML_Functions.hh"

#define XML_PRECISION 16

/*
  Interface for pugi::xml_node class
*/
class XML_Node
{
public:

    // Find a child node
    XML_Node get_child(std::string name);
    
    // Append a child node
    XML_Node append_child(std::string name);

    // Get an attribute of the node, insisting that it exists
    template<typename T> T get_attribute_value(std::string description);

    // Get an attribute of the node given a default
    template<typename T> T get_attribute_value(std::string description,
                                           T def);
    
    // Get the value of the node, insisting that it exists
    template<typename T> T get_value();
    template<typename T> vector<T> vector(int expected_size);
    
    // Get the value of the node given a default
    template<typename T> T get_value(T def);
    template<typename T> vector<T> vector(vector<T> def);
    
    // Get a child value of the node, insisting that it exists
    template<typename T> T get_child_value(std::string description);
    template<typename T> vector<T> get_child_vector(std::string description,
                                                    int expected_size);
    
    // Get a child value of the node given a default
    template<typename T> T get_child_value(std::string description,
                                           T def);
    template<typename T> vector<T> get_child_vector(std::string description,
                                                vector<T> def);

    // Set attribute of node
    template<typename T> void set_attribute(T data,
                                            string description);
    
    // Set value of node
    template<typename T> void set_value(T data);
    template<typename T> void set_vector(vector<T> const &data);

    // Set value of child node
    template<typename T> void set_child_value(T data,
                                              std::string description);
    template<typename T> void set_child_vector(vector<T> const &data,
                                               std::string description);
    
protected:
    
    // Create an XML_Node (see XML_Document for public creation)
    XML_Node(pugi::xml_node node);
    
private:
    
    pugi::xml_node xml_node_;
};

/*
  Definitions for templated functions
*/
template<typename T> T XML_Node::
get_attribute_value(std::string description)
{
    pugi::xml_attribute attr = xml_node_.attribute(description.c_str());

    if (attr.empty())
    {
        string name = static_cast<string>(xml_node_.name());
        string error_message
            = "required attribute ("
            + description
            + ") in node ("
            + name
            + ") not found";
        
        AssertMsg(false, error_message);
    }

    return XML_Functions::attr_value<T>(attr);
}

template<typename T> T XML_Node::
get_attribute_value(std::string description,
                T def)
{
    pugi::xml_attribute attr = xml_node_.attribute(description.c_str());

    if (attr.empty())
    {
        return def;
    }

    return attr_value<T>(text);
}

template<typename T> T XML_Node::
get_value()
{
    pugi::xml_text text = xml_node_.text();
    
    if (text.empty())
    {
        std::string name = static_cast<std::string>(xml_node_.name());
        std::string error_message
            = "required value ("
            + description
            + ") in node ("
            + name
            + ") not found";
        
        AssertMsg(false, error_message);
    }
    
    return XML_Functions::text_value<T>(text);
}

template<typename T> vector<T> XML_Node::
get_vector(int expected_size)
{
    pugi::xml_text text = xml_node_.text();

    if (text.empty())
    {
        std::string name = static_cast<std::string>(xml_node_.name());
        std::string error_message
            = "required value ("
            + description
            + ") in node ("
            + name
            + ") not found";
        
        AssertMsg(false, error_message);
    }
    
    vector<T> value = XML_Functions::text_vector<T>(text);

    if (value.size() != expected_size)
    {
        std::string name = static_cast<std::string>(xml_node_.name());
        std::string error_message
            = "num values of ("
            + description
            + ") in node ("
            + name
            + ") incorrect; expected ("
            + std::to_string(expected_size)
            + ") but got ("
            + std::to_string(value.size())
            + ")";
        
        AssertMsg(false, error_message);
    }

    return value;
}

template<typename T> T XML_Node::
get_value(T def)
{
    pugi::xml_text text = xml_node_.text();

    if (text.empty())
    {
        return def;
    }

    return text_value<T>(text);
}

template<typename T> vector<T> XML_Node::
get_vector(int expected_size,
       vector<T> def)
{
    pugi::xml_text text = xml_node_.text();

    if (text.empty())
    {
        return def;
    }

    vector<T> value = XML_Functions::text_vector<T>(text);

    if (value.size() != expected_size)
    {
        std::string name = static_cast<std::string>(xml_node_.name());
        std::string error_message
            = "size of ("
            + description
            + ") in node ("
            + name
            + ") incorrect - expected ("
            + std::to_string(expected_size)
            + ") but got ("
            + std::to_string(value.size())
            + ") - reverting to default value";
        std::cout << error_message << std::endl;

        return def;
    }
    
    return value;
}

template<typename T> T XML_Node::
get_child_value(std::string description)
{
    return get_child(description).value();
}

template<typename T> vector<T> XML_Node::
get_child_vector(std::string description,
             int expected_size)
{
    return get_child(description).vector(expected_size);
}

template<typename T> T XML_Node::
get_child_value(std::string description,
            T def)
{
    return get_child(description).value(def);
}

template<typename T> vector<T> XML_Node::
get_child_vector(std::string description,
             int expected_size,
             vector<T> def)
{
    return get_child(description).vector(expected_size,
                                         def);
}

template<typename T> void XML_Node::
set_attribute(T data,
              string description)
{
    string data_string;
    String_Functions::to_string(data_string,
                                data,
                                XML_PRECISION);
    
    pugi::xml_attribute attr = xml_node_.append_attribute(description);
    attr.set_value(data_string.c_str());
}

template<typename T> void XML_Node::
set_value(T data)
{
    string data_string;
    String_Functions::to_string(data_string,
                                data,
                                XML_PRECISION);
                                
    xml_node_.append_child(pugi::node_pcdata).set_value(data_string.c_str());
}

template<typename T> void XML_Node::
set_vector(vector<T> const &data,
           string index_order = "")
{
    string data_string;
    String_Functions::vector_to_string(data_string,
                                       data,
                                       XML_PRECISION);

    xml_node_.append_child(pugi::node_pcdata).set_value(data_string.c_str());

    if (index_order != "")
    {
        set_attribute(index_order,
                      "index");
    }
}

template<typename T> void XML_Node::
set_child_value(T data,
                std::string description)
{
    append_node(description).set_value(data);
}

template<typename T> void XML_Node::
set_child_vector(vector<T> const &data,
                 std::string description,
                 string index_order)
{
    append_node(description).set_vector(data,
                                        index_order);
}

#endif