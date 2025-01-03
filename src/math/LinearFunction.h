/*
    Scan Tailor - Interactive post-processing tool for scanned pages.
    Copyright (C) 2015  Joseph Artsimovich <joseph.artsimovich@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef LINEAR_FUNCTION_H_
#define LINEAR_FUNCTION_H_

#include "math_config.h"
#include <Eigen/Core>
#include <cstddef>

/**
 * A linear function from arbitrary number of variables
 * expressed in matrix form:
 * \code
 * F(x) = a^T * x + b
 * \endcode
 */
class MATH_EXPORT LinearFunction
{
	// Member-wise copying is OK.
public:
	Eigen::VectorXd a;
	double b;

	/**
	 * Constructs a linear function of the given number of variables,
	 * initializing everything to zero.
	 */
	LinearFunction(size_t num_vars = 0);

	/**
	 * Resets everything to zero, so that F(x) = 0
	 */
	void reset();

	size_t numVars() const { return a.size(); }

	/**
	 * Evaluates a^T * x + b
	 */
	double evaluate(Eigen::VectorXd const& x) const;

	void swap(LinearFunction& other);

	LinearFunction& operator+=(LinearFunction const& other);

	LinearFunction& operator*=(double scalar);
};


inline void swap(LinearFunction& f1, LinearFunction& f2)
{
	f1.swap(f2);
}

#endif
