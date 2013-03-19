/**
 * @file range.hpp
 *
 *  Created on: 13.03.2013
 *      @author: Sergei A. Fedorov (sergei.a.fedorov at gmail dot com)
 */

#ifndef SHAMAN_UTIL_RANGED_VALUE_HPP_
#define SHAMAN_UTIL_RANGED_VALUE_HPP_

#include <stdexcept>

namespace shaman {
namespace util {

// TODO out_of_range error

/**
 * Type representing an integral value that can contain a value in a certain
 * range (the range ends are inclusive).
 */
template < typename T, T MinVal, T MaxVal, T DefaultVal = 0 >
struct ranged_value {
/* TODO Concept check */
	typedef T value_type;
	typedef ranged_value< T, MinVal, MaxVal, DefaultVal > this_type;

	enum {
		minimum = MinVal,
		maximum = MaxVal,
		default_value = DefaultVal
	};

	ranged_value() :
		val_(default_value) {}

	/**
	 * Non-explicit constructor
	 * @param v
	 */
	ranged_value(value_type v) :
		val_(v)
	{
		check_range();
	}

	/**
	 * Non-explicit converting constructor
	 * @param v
	 */
	template < typename U >
	ranged_value(U v) :
		val_(v)
	{
		check_range();
	}

	/**
	 * Non-explicit converting constructor from another ranged value
	 * @param rhs
	 */
	template < typename U, U Min, U Max, U Def >
	ranged_value( ranged_value<U, Min, Max, Def > const& rhs ) :
		val_( rhs.value() )
	{
	}

	void
	swap( this_type& rhs )
	{
		using std::swap;
		swap( val_, rhs.val_ );
	}

	//@{
	/** @name Comparison operators */

	//@}

	//@{
	/** @name Assignment operators */
	this_type&
	operator = ( value_type v )
	{
		this_type tmp(v);
		swap(tmp);
		return *this;
	}

	this_type&
	operator = ( this_type const& rhs )
	{
		this_type tmp(rhs);
		swap(tmp);
		return *this;
	}

	this_type&
	operator += ( value_type rhs )
	{
		this_type tmp(val_ + rhs);
		swap(tmp);
		return *this;
	}

	this_type&
	operator -= ( value_type rhs )
	{
		this_type tmp(val_ - rhs);
		swap(tmp);
		return *this;
	}

	this_type&
	operator *= ( value_type rhs )
	{
		this_type tmp(val_ * rhs);
		swap(tmp);
		return *this;
	}

	this_type&
	operator /= ( value_type rhs )
	{
		this_type tmp(val_ / rhs);
		swap(tmp);
		return *this;
	}

	this_type&
	operator <<= ( size_t bits )
	{
		this_type tmp(val_ << bits);
		swap(tmp);
		return *this;
	}

	this_type&
	operator >>= ( size_t bits )
	{
		this_type tmp(val_ >> bits);
		swap(tmp);
		return *this;
	}
	//@}

	//@{
	/** @name Negation and complement */
	this_type
	operator - () const
	{
		return this_type(-val_);
	}

	this_type
	operator ~ () const
	{
		return this_type(~val_);
	}
	//@}

	//@{
	/** @name Comparison operators */

	//@}

	//@{
	/** @name Implicit conversion */
	operator value_type () const
	{
		return val_;
	}
	//@}

	value_type
	value() const
	{
		return val_;
	}

	bool
	defaulted() const
	{
		return val_ == default_value;
	}
private:
	void
	check_range()
	{
		if (val_ < minimum || val_ > maximum)
			throw std::out_of_range("Value is out of range");
	}
private:
	value_type val_;
};

//@{
/** @name Arithmetic operators */

//@}

} // namespace util
} // namespace shaman

#endif /* SHAMAN_UTIL_RANGED_VALUE_HPP_ */
