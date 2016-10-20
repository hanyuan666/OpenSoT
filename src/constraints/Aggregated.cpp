/*
 * Copyright (C) 2014 Walkman
 * Author: Alessio Rocchi
 * email:  alessio.rocchi@iit.it
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU Lesser General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/

#include <OpenSoT/constraints/Aggregated.h>

#include <assert.h>
#include <limits>
#include <sstream>

using namespace OpenSoT::constraints;

Aggregated::Aggregated(const std::list<ConstraintPtr> bounds,
                       const Eigen::VectorXd &q,
                       const unsigned int aggregationPolicy) :
    Constraint(concatenateConstraintsIds(bounds), q.size()),
               _bounds(bounds), _aggregationPolicy(aggregationPolicy)
{
    assert(bounds.size()>0);

    this->checkSizes();
    /* calling update to generate bounds */
    this->update(q);
}

Aggregated::Aggregated(const std::list<ConstraintPtr> bounds,
                       const unsigned int x_size,
                       const unsigned int aggregationPolicy) :
    Constraint(concatenateConstraintsIds(bounds), x_size),
               _bounds(bounds), _aggregationPolicy(aggregationPolicy)
{
    this->checkSizes();
    /* calling update to generate bounds */
    this->generateAll();
}

Aggregated::Aggregated(ConstraintPtr bound1,
                       ConstraintPtr bound2,
                       const unsigned int &x_size,
                       const unsigned int aggregationPolicy) :
    Constraint(bound1->getConstraintID() + "+" + bound2->getConstraintID(),
               x_size), _aggregationPolicy(aggregationPolicy)
{
    _bounds.push_back(bound1);
    _bounds.push_back(bound2);

    this->checkSizes();
    /* calling update to generate bounds */
    this->generateAll();
}

void Aggregated::update(const Eigen::VectorXd& x) {
    /* iterating on all bounds.. */
    for(typename std::list< ConstraintPtr >::iterator i = _bounds.begin();
        i != _bounds.end(); i++) {

        ConstraintPtr &b = *i;
        /* update bounds */
        b->update(x);
    }

    this->generateAll();
}

void Aggregated::generateAll() {
    /* resetting all internal data */
    _upperBound.resize(0);
    _lowerBound.resize(0);

    _Aeq.resize(0,_x_size);
    _beq.resize(0);

    _Aineq.resize(0,_x_size);
    _bUpperBound.resize(0);
    _bLowerBound.resize(0);

    /* iterating on all bounds.. */
    for(typename std::list< ConstraintPtr >::iterator i = _bounds.begin();
        i != _bounds.end(); i++) {

        ConstraintPtr &b = *i;

        Eigen::VectorXd boundUpperBound = b->getUpperBound();
        Eigen::VectorXd boundLowerBound = b->getLowerBound();

        Eigen::MatrixXd boundAeq = b->getAeq();
        Eigen::VectorXd boundbeq = b->getbeq();

        Eigen::MatrixXd boundAineq = b->getAineq();
        Eigen::VectorXd boundbUpperBound = b->getbUpperBound();
        Eigen::VectorXd boundbLowerBound = b->getbLowerBound();

        /* copying lowerBound, upperBound */
        if(boundUpperBound.size() != 0 ||
           boundLowerBound.size() != 0) {
            assert(boundUpperBound.size() == _x_size);
            assert(boundLowerBound.size() == _x_size);

            if(_upperBound.size() == 0 ||
               _lowerBound.size() == 0) { // first valid bounds found
                assert(_upperBound.size() == _lowerBound.size());
                _upperBound = boundUpperBound;
                _lowerBound = boundLowerBound;
            } else {
                for(unsigned int i = 0; i < _x_size; ++i) {
                    // compute the minimum between current and new upper bounds
                    _upperBound[i] = std::min(  _upperBound[i],
                                                boundUpperBound[i]);
                    // compute the maximum between current and new lower bounds
                    _lowerBound[i] = std::max(  _lowerBound[i],
                                                boundLowerBound[i]);
                }
            }
        }

        /* copying Aeq, beq */
        if( boundAeq.rows() != 0 ||
            boundbeq.size() != 0) {
            assert(boundAeq.rows() == boundbeq.size());
            /* when transforming equalities to inequalities,
                Aeq*x = beq becomes
                beq <= Aeq*x <= beq */
            if(_aggregationPolicy & EQUALITIES_TO_INEQUALITIES) {
                assert(_Aineq.cols() == boundAeq.cols());
                pile(_Aineq,boundAeq);
                pile(_bUpperBound,boundbeq);
                if(_aggregationPolicy & UNILATERAL_TO_BILATERAL) {
                    pile(_bLowerBound,boundbeq);
                /* we want to have only unilateral constraints, so
                   beq <= Aeq*x <= beq becomes
                   -Aeq*x <= -beq && Aeq*x <= beq */
                } else {
                    assert(_Aineq.cols() == boundAeq.cols());
                    pile(_Aineq,-1.0*boundAeq);
                    pile(_bUpperBound, -1.0 * boundbeq);
                }
            } else {
                assert(_Aeq.cols() == boundAeq.cols());
                pile(_Aeq,boundAeq);
                pile(_beq,boundbeq);
            }
        }

        /* copying Aineq, bUpperBound, bLowerBound*/
        if( boundAineq.rows() != 0 ||
            boundbUpperBound.size() != 0 ||
            boundbLowerBound.size() != 0) {

            assert(boundAineq.rows() > 0);
            assert(boundbLowerBound.size() > 0 ||
                   boundbUpperBound.size() > 0);
            assert(boundAineq.cols() == _x_size);

            /* if we need to transform all unilateral bounds to bilateral.. */
            if(_aggregationPolicy & UNILATERAL_TO_BILATERAL) {
                if(boundbUpperBound.size() == 0) {
                    assert(boundAineq.rows() == boundbLowerBound.size());
                    boundbUpperBound.resize(boundAineq.rows());
                    boundbUpperBound<<boundbUpperBound.setOnes(boundAineq.rows())*std::numeric_limits<double>::infinity();
                } else if(boundbLowerBound.size() == 0) {
                    assert(boundAineq.rows() == boundbUpperBound.size());
                    boundbLowerBound.resize(boundAineq.rows());
                    boundbLowerBound<<boundbLowerBound.setOnes(boundAineq.rows())*-std::numeric_limits<double>::max();
                } else {
                    assert(boundAineq.rows() == boundbLowerBound.size());
                    assert(boundAineq.rows() == boundbUpperBound.size());
                }
            /* if we need to transform all bilateral bounds to unilateral.. */
            } else {
                /* we need to transform l < Ax into -Ax < -l */
                if(boundbUpperBound.size() == 0) {
                    boundAineq = -1.0 * boundAineq;
                    boundbLowerBound = -1.0 * boundbLowerBound;
                    assert(boundAineq.rows() == boundbLowerBound.size());
                } else if(boundbLowerBound.size() == 0) {
                    assert(boundAineq.rows() == boundbUpperBound.size());
                } else {
                    assert(boundAineq.rows() == boundbLowerBound.size());
                    assert(boundAineq.rows() == boundbUpperBound.size());
                    pile(boundAineq,-1.0 * boundAineq);
                    pile(boundbUpperBound,-1.0 * boundbLowerBound);
                }
            }

            assert(_Aineq.cols() == boundAineq.cols());
            pile(_Aineq,boundAineq);
            pile(_bUpperBound,boundbUpperBound);
            /*  if using UNILATERAL_TO_BILATERAL we always have lower bounds,
                otherwise, we never have them */
            if(_aggregationPolicy & UNILATERAL_TO_BILATERAL)
                pile(_bLowerBound, boundbLowerBound);
        }
    }

    /* checking everything went fine */
    assert(_lowerBound.size() == 0 || _lowerBound.size() == _x_size);
    assert(_upperBound.size() == 0 || _upperBound.size() == _x_size);

    assert(_Aeq.rows() == _beq.size());
    if(_Aeq.rows() > 0)
        assert(_Aeq.cols() == _x_size);

    assert(_Aineq.rows() == _bUpperBound.size());
    if(!(_aggregationPolicy & UNILATERAL_TO_BILATERAL))
        assert(_Aineq.rows() == _bLowerBound.size());
    if(_Aineq.rows() > 0)
        assert(_Aineq.cols() == _x_size);
}

void Aggregated::checkSizes()
{
    for(std::list< ConstraintPtr >::iterator i = _bounds.begin();
        i != _bounds.end(); ++i) {
        ConstraintPtr t = *i;
        assert(this->getXSize() == t->getXSize());
    }
}

const std::string Aggregated::concatenateConstraintsIds(const std::list<ConstraintPtr> constraints) {
    std::string concatenatedId;
    int constraintSize = constraints.size();
    for(std::list<ConstraintPtr>::const_iterator i = constraints.begin(); i != constraints.end(); ++i) {
        concatenatedId += (*i)->getConstraintID();
        if(--constraintSize > 0)
            concatenatedId += "+";
    }
    return concatenatedId;
}
