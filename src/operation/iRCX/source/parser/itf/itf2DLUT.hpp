#pragma once 

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace itf
{

// look-up table
// query value by row column
template<typename T1, typename T2, typename T3>
class itf2DLUT {
 public: 
  // constructor
  itf2DLUT() = default;
  explicit itf2DLUT(const char* row_name, const char* col_name, const char* value_name)
  : _rows(),
    _cols(),
    _values(),
    _row_name(row_name),
    _col_name(col_name),
    _value_name(value_name)
  { }
  itf2DLUT(const itf2DLUT& other)
  {
    *this = other;
  }

  // getter
  const std::vector<T1>& get_rows()   const   { return _rows;       }
  const std::vector<T2>& get_cols()   const   { return _cols;       }
  const std::vector<T3>& get_values() const   { return _values;     }
  std::string get_row_name()          const   { return _row_name;   }
  std::string get_col_name()          const   { return _col_name;   }
  std::string get_value_name()        const   { return _value_name; }

  // setter
  void add_row_data(T1 e) { _rows.push_back(e);   }
  void add_col_data(T2 e) { _cols.push_back(e);   }
  void add_value(T3 e)    { _values.push_back(e); }
  void set_row_name(const char* s)     { _row_name = s;    }
  void set_col_name(const char* s)     { _col_name = s;    }
  void set_value_name(const char* s)   { _value_name = s;  }

  // operator
  itf2DLUT& operator=(const itf2DLUT& rhs) {
    if (this == &rhs) return *this;

    _rows = rhs._rows;
    _cols = rhs._cols;
    _values = rhs._values;
    _row_name = rhs._row_name;
    _col_name = rhs._col_name;
    _value_name = rhs._value_name;

    return *this;
  }

  bool operator==(const itf2DLUT& rhs) const {
    if (this == &rhs) return true;

    return _row_name == rhs._row_name
      && _col_name == rhs._col_name
      && _value_name == rhs._value_name
      && _rows == rhs._rows
      && _cols == rhs._cols
      && _values == rhs._values
    ;
  }

  // function
  
  void clear() {
    _rows.clear();
    _cols.clear();
    _values.clear();
    _row_name.clear();
    _col_name.clear();
    _value_name.clear();
  }

  // @param r_idx row index
  // @param c_idx col index
  std::optional<T3> query(int r_idx, int c_idx) const {
    int v_idx = r_idx * _cols.size() + c_idx;
    if ((0 <= r_idx) && (r_idx < (int)_rows.size()  )
     && (0 <= c_idx) && (c_idx < (int)_cols.size()  ) 
     && (0 <= v_idx) && (v_idx < (int)_values.size()) )
    {
      return _values.at(v_idx);
    } else {
      return std::nullopt;
    }
  }

  // bilinear interpolation for points inside of the range of data points,
  // keep boundary value for points outside of the range.
  // In other words, no extrapolate beyond the table.
  // @param r data in _raws 
  // @param c data in _cols
  std::optional<T3> query_interpolation(const T1& r, const T2& c) const {
    if (_rows.empty() || _cols.empty()) return std::nullopt;
    
    // 找到行边界索引
    auto r_it = std::lower_bound(_rows.begin(), _rows.end(), r);
    size_t r_idx;
    
    if (r_it == _rows.end()) {
      r_idx = _rows.size() - 1; // r 大于等于所有行
    } else if (r_it == _rows.begin()) {
      r_idx = 0; // r 小于等于第一行
    } else {
      r_idx = std::distance(_rows.begin(), r_it); // r 在两个行之间，需要插值
    }
    
    // 找到列边界索引（类似）
    auto c_it = std::lower_bound(_cols.begin(), _cols.end(), c);
    size_t c_idx;
    
    if (c_it == _cols.end()) {
      c_idx = _cols.size() - 1;
    } else if (c_it == _cols.begin()) {
      c_idx = 0;
    } else {
      c_idx = std::distance(_cols.begin(), c_it);
    }
    
    // 确定是否需要插值
    bool r_at_boundary = (r_idx == 0 || r_idx == _rows.size() - 1);
    bool c_at_boundary = (c_idx == 0 || c_idx == _cols.size() - 1);
    
    // 如果查询点在边界或之外，直接返回最近的网格点
  if (r_at_boundary || c_at_boundary) {
      // 确定最近的行列索引
      size_t nearest_r = r_idx;
      size_t nearest_c = c_idx;
      
      // 如果 r 小于第一行，用第一行
      if (r < _rows[0]) nearest_r = 0;
      // 如果 r 大于最后一行，用最后一行
      else if (r > _rows.back()) nearest_r = _rows.size() - 1;
      // 否则 r 在范围内，但可能靠近边界
      
      // 列类似处理
      if (c < _cols[0]) nearest_c = 0;
      else if (c > _cols.back()) nearest_c = _cols.size() - 1;
      
      return query(nearest_r, nearest_c);
    }
    
    // 完全在内部的情况，进行双线性插值
    size_t r_low = r_idx - 1;
    size_t r_high = r_idx;
    size_t c_low = c_idx - 1;
    size_t c_high = c_idx;
    
    auto v_rl_cl = query(r_low, c_low);
    auto v_rl_ch = query(r_low, c_high);
    auto v_rh_cl = query(r_high, c_low);
    auto v_rh_ch = query(r_high, c_high);
    
    if (v_rl_cl && v_rl_ch && v_rh_cl && v_rh_ch) {
      // 列插值
      double col_ratio = (c - _cols[c_low]) / (_cols[c_high] - _cols[c_low]);
      auto v_rl_cmid = std::lerp(*v_rl_cl, *v_rl_ch, col_ratio);
      auto v_rh_cmid = std::lerp(*v_rh_cl, *v_rh_ch, col_ratio);
      
      // 行插值
      double row_ratio = (r - _rows[r_low]) / (_rows[r_high] - _rows[r_low]);
      return std::lerp(v_rl_cmid, v_rh_cmid, row_ratio);
    }
    
    return std::nullopt;
  }

  // @param list_name data_list name. match data_list in an order of rows, cols and values.
  template<typename E>
  void add_data(const char* list_name, E e) {
    if (_row_name.compare(list_name) == 0) {
      add_row_data(T1(e));
    } else if (_col_name.compare(list_name) == 0) {
      add_col_data(T2(e));
    } else if (_value_name.compare(list_name) == 0) {
      add_value(T3(e));
    } else {
      std::cout << "fail to find data list named " << list_name << std::endl;
    }
  }

  template<typename E>
  void set_data_list(const char* list_name, const std::vector<E>& src) {
    if (_row_name.compare(list_name) == 0) {
      set_list<T1, E>(&_rows, src);
    } else if (_col_name.compare(list_name) == 0) {
      set_list<T2, E>(&_cols, src);
    } else if (_value_name.compare(list_name) == 0) {
      set_list<T3, E>(&_values, src);
    } else {
      std::cout << "fail to find data list named " << list_name << std::endl;
    }
  }

  // @param r rows_name
  // @param c cols_name
  // @param v values_name
  void set_names(const char* r, const char* c, const char* v) {
    set_row_name(r);
    set_col_name(c);
    set_value_name(v);
  }

 protected:
  // members
  std::vector<T1> _rows;
  std::vector<T2> _cols;
  std::vector<T3> _values;
  std::string _row_name;
  std::string _col_name;
  std::string _value_name;
 
 private:
  // function

  template<typename E1, typename E2>
  void set_list(void* dst, const std::vector<E2>& src) {
    if (typeid(E1) == typeid(E2)) {
      std::vector<E2>* dst_ptr = (std::vector<E2>*)dst;
      dst_ptr->clear();
      for (auto& e : src) {
        dst_ptr->push_back(e);
      }
    } else {
      std::cout << "data type mismatch" << std::endl;
    }
  }
};

// 2D look-up table with title
template<typename T1, typename T2, typename T3>
class itfTitleLut : public itf2DLUT<T1, T2, T3> {
 public:
  // constructor
  itfTitleLut() : itf2DLUT<T1, T2, T3>(), _title() { }

  itfTitleLut(const char* title, const char* row_name, const char* col_name, const char* value_name)
  : itf2DLUT<T1, T2, T3>(row_name, col_name, value_name),
    _title(title)
  { }

  itfTitleLut(const itfTitleLut& other)
  : itf2DLUT<T1, T2, T3>(other),
    _title(other._title)
  { }

  itfTitleLut(const char* title, const itf2DLUT<T1, T2, T3> lut)
  : itf2DLUT<T1, T2, T3>(lut),
    _title(title)
  { }

  // getter
  std::string get_title() const { return _title; }

  // setter
  void set_title(const char* title) { _title = title; }
  void set_lut(const itf2DLUT<T1, T2, T3>& lut) {
    static_cast<itf2DLUT<T1, T2, T3>&>(*this) = lut;
  }

  // operator
  itfTitleLut& operator=(const itfTitleLut& rhs) {
    if (this == &rhs) return *this;

    static_cast<itf2DLUT<T1, T2, T3>&>(*this) = rhs;
    _title = rhs._title;

    return *this;
  }

  bool operator==(const itfTitleLut& rhs) const {
    if (this == &rhs) return true;

    return _title == rhs._title
      && static_cast<const itf2DLUT<T1, T2, T3>&>(*this) == rhs
    ;
  }

  // function
  void clear() {
    itf2DLUT<T1, T2, T3>::clear();
    _title.clear();
  }

 protected:
  std::string _title;
};

} // namespace itf