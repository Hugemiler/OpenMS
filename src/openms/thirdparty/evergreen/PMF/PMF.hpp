#ifndef _PMF_HPP
#define _PMF_HPP

#include "../Utility/Clock.hpp"
#include "../Convolution/p_convolve.hpp"
#include "marginal.hpp"
#include "nonzero_bounding_box.hpp"

// Forward declarations to allow ostream << PMF in this file.
class PMF;
std::ostream & operator <<(std::ostream & os, const PMF & rhs);

class PMF {
public:
  static constexpr double mass_threshold_for_normalization = 0.0;
  static constexpr double relative_mass_threshold_for_bounding_box = 0.0;

protected:
  
  Vector<long> _first_support;
  Tensor<double> _table;

  void narrow_to_nonzero_support() {
    std::array<Vector<unsigned long>, 2> nonzero_box = nonzero_bounding_box(_table, relative_mass_threshold_for_bounding_box);

    narrow_support(_first_support + nonzero_box[0], _first_support + nonzero_box[1]);
  }

  void normalize() {
    double tot = sum(_table.flat());
    #ifdef NUMERIC_CHECK
    assert(tot > mass_threshold_for_normalization);
    #endif

    _table.flat() /= tot;
  }

  void verify_nonnegative() const {
    assert( _table.flat() >= 0.0 && "PMF must be constructed from nonnegative Tensor<double>" );
  }
public:
  // Construct dimension 0 by default.
  PMF() { }
  
  PMF(const Vector<long> & sup, const Tensor<double> & tab):
    _first_support(sup),
    _table(tab)
  {
    #ifdef SHAPE_CHECK
    assert(_first_support.size() == _table.dimension());
    #endif
    #ifdef NUMERIC_CHECK
    verify_nonnegative();
    #endif

    normalize();
    narrow_to_nonzero_support();
  }

  PMF(const Vector<long> & sup, Tensor<double> && tab):
    _first_support(sup),
    _table(std::move(tab))
  {
    #ifdef SHAPE_CHECK
    assert(_first_support.size() == _table.dimension());
    #endif
    #ifdef NUMERIC_CHECK
    verify_nonnegative();
    #endif

    normalize();
    narrow_to_nonzero_support();
  }

  PMF(Vector<long> && sup, Tensor<double> && tab):
    _first_support(sup),
    _table(std::move(tab))
  {
    #ifdef SHAPE_CHECK
    assert(_first_support.size() == _table.dimension());
    #endif
    #ifdef NUMERIC_CHECK
    verify_nonnegative();
    #endif

    normalize();
    narrow_to_nonzero_support();
  }

  PMF(const PMF & rhs):
    _first_support(rhs._first_support),
    _table(rhs._table)
  {
    // Do not need to normalize or check bounding box
  }

  PMF(PMF && rhs):
    _first_support(std::move(rhs._first_support)),
    _table(std::move(rhs._table))
  {
    // Do not need to normalize or check bounding box
  }

  const PMF & operator =(const PMF & rhs) {
    _first_support = rhs._first_support;
    _table = rhs._table;
    return *this;
  }

  const PMF & operator =(PMF && rhs) {
    _first_support = std::move(rhs._first_support);
    _table = std::move(rhs._table);
    return *this;
  }

  void narrow_support(const Vector<long> & new_first_support, const Vector<long> & new_last_support) {
    #ifdef SHAPE_CHECK
    assert(dimension() == new_first_support.size() && new_first_support.size() == new_last_support.size());
    assert(new_first_support <= new_last_support);
    #endif

    Vector<long> intersecting_first_support = _first_support;

    Vector<unsigned long> new_shape(new_last_support.size());
    for (unsigned char i=0; i<new_last_support.size(); ++i)
      new_shape[i] = new_last_support[i] - new_first_support[i] + 1ul;
    for (unsigned char i=0; i<new_shape.size(); ++i) {
      long new_last = std::min(new_last_support[i], (long)(intersecting_first_support[i] + _table.data_shape()[i]) - 1);
      intersecting_first_support[i] = std::max(intersecting_first_support[i], new_first_support[i]);

      long new_shape_i = new_last - intersecting_first_support[i] + 1;
      #ifdef SHAPE_CHECK
      if (new_shape_i <= 0) {
	std::cerr << "Narrowing to " << new_first_support << " " << new_last_support << " results in empty PMF" << std::endl;
	assert(false);
      }
      #endif

      new_shape[i] = (unsigned long) new_shape_i;
    }

    // intersecting_first_support will only have increased compared to
    // _first_support:
    Vector<unsigned long> tensor_start = intersecting_first_support - _first_support;
    _table.shrink(tensor_start, new_shape);
    normalize();

    copy(_first_support, intersecting_first_support);
  }

  unsigned char dimension() const {
    return _first_support.size();
  }

  const Tensor<double> & table() const {
    return _table;
  }

  const Vector<long> & first_support() const {
    return _first_support;
  }
  // Note: The following could also be cached during construction, but
  // it isn't really a large performance benefit and would take up
  // more memory and make construction more expensive.
  Vector<long> last_support() const {
    return _first_support + _table.view_shape() - 1L;
  }

  PMF marginal(const Vector<unsigned char> & axes_to_keep, double p) const {
    #ifdef SHAPE_CHECK
    verify_subpermutation(axes_to_keep, dimension());
    #endif

    if (axes_to_keep.size() == dimension())
      // all axes are kept (tranpose to avoid pow computation and
      // normalization)
      return transposed(axes_to_keep);

    if ( axes_to_keep.size() == 0 )
      return PMF();

    Vector<long> new_first_support(axes_to_keep.size());
    unsigned char k;
    for (k=0; k<axes_to_keep.size(); ++k)
      new_first_support[k] = _first_support[ axes_to_keep[k] ];

    return PMF( new_first_support, ::marginal(_table, axes_to_keep, p));
  }

  PMF transposed(const Vector<unsigned char> & new_order) const {
    #ifdef SHAPE_CHECK
    assert(new_order.size() == dimension());
    verify_permutation(new_order);
    #endif

    // Does not need to renormalize:
    PMF result(*this);
    result.transpose(new_order);
    
    return result;
  }

  void transpose(const Vector<unsigned char> & new_order) {
    #ifdef SHAPE_CHECK
    assert(new_order.size() == dimension());
    verify_permutation(new_order);
    #endif
    
    Vector<long> new_first_support(new_order.size());
    for (unsigned char i=0; i<new_first_support.size(); ++i)
      new_first_support[i] = _first_support[ new_order[i] ];
    _first_support = std::move(new_first_support);

    ::transpose(_table, new_order);
  }
};

inline PMF p_add(const PMF & lhs, const PMF & rhs, double p) {
  #ifdef SHAPE_CHECK
  assert(lhs.table().dimension() == rhs.table().dimension());
  #endif

  return PMF(lhs.first_support() + rhs.first_support(), numeric_p_convolve(lhs.table(), rhs.table(), p) );
}

inline PMF p_sub(const PMF & lhs, const PMF & rhs, double p) {
  #ifdef SHAPE_CHECK
  assert(lhs.table().dimension() == rhs.table().dimension());
  #endif

  // Flip the rhs table along every axis so that addition of the
  // flipped table corresponds to subtraction with the original table:
  Tensor<double> rhs_table_flipped(rhs.table().data_shape());
  Vector<unsigned long> counter_flipped(lhs.dimension());
  enumerate_for_each_tensors([&rhs_table_flipped, &counter_flipped](const_tup_t counter, const unsigned char dim, double val){
      for (unsigned char i=0; i<dim; ++i)
	counter_flipped[i] = rhs_table_flipped.data_shape()[i] - counter[i] - 1ul;

      rhs_table_flipped[ tuple_to_index(counter_flipped, rhs_table_flipped.data_shape(), dim) ] = val;
    },
    rhs_table_flipped.data_shape(),
    rhs.table());

  return PMF(lhs.first_support() - rhs.last_support(), numeric_p_convolve(lhs.table(), rhs_table_flipped, p) );
}

inline std::ostream & operator <<(std::ostream & os, const PMF & rhs) {
  os << "PMF:" << "{"<< rhs.first_support() << " to " << rhs.last_support() << "} " << rhs.table();
  return os;
}

#include "scaled_pmf.hpp"
#include "scaled_pmf_interpolate.hpp"
#include "scaled_pmf_dither.hpp"
#include "scaled_pmf_dither_interpolate.hpp"

#endif
