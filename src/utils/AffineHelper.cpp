#include <OpenSoT/utils/AffineHelper.h>


int OpenSoT::OptvarHelper::getSize() const
{
    return _size;
}

OpenSoT::OptvarHelper::OptvarHelper(std::vector< std::pair< std::string, int > > name_size_pairs)
{
    _size = 0;
    
    for(auto pair : name_size_pairs){

        if( _vars_map.count(pair.first) ){
            throw std::invalid_argument("Duplicate variable names are not allowed");
        }
            
        
        VarInfo vinfo;
        vinfo.size = pair.second;
        vinfo.start_idx = _size;
        
        _size += vinfo.size;
        
        _vars.push_back(vinfo);
        _vars_map[pair.first] = vinfo;
        
    }
}

OpenSoT::AffineHelper OpenSoT::OptvarHelper::getVar(std::string name) const
{
    auto it = _vars_map.find(name);
    
    if( it == _vars_map.end() ){
        throw std::invalid_argument("Variable does not exist");
    }
    
    
    
    Eigen::MatrixXd M;
    Eigen::VectorXd q;
    
    M.setZero(it->second.size, _size);
    q.setZero(it->second.size);
    
    M.block(0, it->second.start_idx, M.rows(), it->second.size) = Eigen::MatrixXd::Identity(M.rows(), it->second.size);
    
    return OpenSoT::AffineHelper(M, q);
    
}


























































