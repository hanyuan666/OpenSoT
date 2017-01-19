/*
 * Copyright (C) 2016 Cogimon
 * Authors: Enrico Mingo Hoffman, Alessio Rocchi
 * email:  enrico.mingo@iit.it, alessio.rocchi@iit.it
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

#include <OpenSoT/tasks/torque/CartesianImpedanceCtrl.h>
#include <idynutils/cartesian_utils.h>
#include <exception>
#include <cmath>

using namespace OpenSoT::tasks::torque;

#define LAMBDA_THS 1E-12

CartesianImpedanceCtrl::CartesianImpedanceCtrl(std::string task_id,
                     const Eigen::VectorXd& x,
                     XBot::ModelInterface &robot,
                     std::string distal_link,
                     std::string base_link) :
    Task(task_id, x.size()), _robot(robot),
    _distal_link(distal_link), _base_link(base_link),
    _desiredTwist(6), _use_inertia_matrix(true)
{
    _desiredTwist.setZero(_desiredTwist.size());

    this->_base_link_is_world = (_base_link == WORLD_FRAME_NAME);

    if(!this->_base_link_is_world) {
        this->_base_link_index = robot.getLinkID(_base_link);
        assert(this->_base_link_index >= 0);
    }

    this->_distal_link_index = robot.getLinkID(_distal_link);
    assert(this->_distal_link_index >= 0);

    if(!this->_base_link_is_world)
        assert(this->_distal_link_index != _base_link_index);

    _K.resize(6, 6);
    _K.setIdentity(_K.rows(), _K.cols()); _K = 100.0*_K;
    _D.resize(6, 6);
    _D.setIdentity(_D.rows(), _D.cols()); _D = 1.0*_D;

    /* first update. Setting desired pose equal to the actual pose */
    this->_update(x);

    _W.resize(_A.rows(), _A.rows());
    _W.setIdentity(_W.rows(), _W.cols());

    _lambda = 1.0;

    _hessianType = HST_SEMIDEF;
}

CartesianImpedanceCtrl::~CartesianImpedanceCtrl()
{
}

void CartesianImpedanceCtrl::_update(const Eigen::VectorXd &x) {

    /************************* COMPUTING TASK *****************************/

    if(_base_link_is_world) {
        bool res =_robot.getJacobian(_distal_link,_A);
        assert(res);
    } else{
        bool res = _robot.getRelativeJacobian(_distal_link, _base_link, _A);
        assert(res);
    }
    _J = _A;

    if (_use_inertia_matrix)
    {
        _robot.getInertiaMatrix(_M);
        _A = _J*_M.inverse();
    }


    if(_base_link_is_world)
        _robot.getPose(_distal_link, _tmp_affine);
    else
        _robot.getPose(_base_link, _distal_link, _tmp_affine);
    _actualPose = _tmp_affine.matrix();

    if(_desiredPose.rows() == 0) {
        /* initializing to zero error */
        _desiredPose = _actualPose;
        _b.setZero(_b.size());
    }

    this->update_b();

    this->_desiredTwist.setZero(_desiredTwist.size());

    /**********************************************************************/
}

void CartesianImpedanceCtrl::setStiffness(const Eigen::MatrixXd &Stiffness)
{
    if(Stiffness.cols() == 6 && Stiffness.rows() == 6)
        _K = Stiffness;
}

void CartesianImpedanceCtrl::setDamping(const Eigen::MatrixXd &Damping)
{
    if(Damping.cols() == 6 && Damping.rows() == 6)
        _D = Damping;
}

void CartesianImpedanceCtrl::setStiffnessDamping(const Eigen::MatrixXd &Stiffness, const Eigen::MatrixXd &Damping)
{
    setStiffness(Stiffness);
    setDamping(Damping);
}

Eigen::MatrixXd CartesianImpedanceCtrl::getStiffness()
{
    return _K;
}

Eigen::MatrixXd CartesianImpedanceCtrl::getDamping(){
    return _D;
}

void CartesianImpedanceCtrl::getStiffnessDamping(Eigen::MatrixXd &Stiffness, Eigen::MatrixXd &Damping)
{
    Stiffness = getStiffness();
    Damping = getDamping();
}

void CartesianImpedanceCtrl::setReference(const Eigen::MatrixXd& desiredPose) {
    assert(desiredPose.rows() == 4);
    assert(desiredPose.cols() == 4);

    _desiredPose = desiredPose;
    _desiredTwist.setZero(_desiredTwist.size());
    this->update_b();
}

void CartesianImpedanceCtrl::setReference(const Eigen::MatrixXd &desiredPose,
                                                       const Eigen::VectorXd &desiredTwist)
{
    assert(desiredTwist.size() == 6);
    assert(desiredPose.rows() == 4);
    assert(desiredPose.cols() == 4);

    _desiredPose = desiredPose;
    _desiredTwist = desiredTwist;
    this->update_b();
}

const Eigen::MatrixXd CartesianImpedanceCtrl::getReference() const {
    return _desiredPose;
}

void CartesianImpedanceCtrl::getReference(Eigen::MatrixXd &desiredPose,
                                                       Eigen::VectorXd &desiredTwist) const
{
    desiredPose = _desiredPose;
    desiredTwist = _desiredTwist;
}

const Eigen::MatrixXd CartesianImpedanceCtrl::getActualPose() const
{
    return _actualPose;
}

const KDL::Frame CartesianImpedanceCtrl::getActualPoseKDL() const
{
    KDL::Frame actualPoseKDL;
    Eigen::Affine3d tmp; tmp.matrix() = _actualPose;
    tf::transformEigenToKDL(tmp, actualPoseKDL);
    return actualPoseKDL;
}

const std::string CartesianImpedanceCtrl::getDistalLink() const
{
    return _distal_link;
}

const std::string CartesianImpedanceCtrl::getBaseLink() const
{
    return _base_link;
}

const bool CartesianImpedanceCtrl::baseLinkIsWorld() const
{
    return _base_link_is_world;
}

Eigen::VectorXd CartesianImpedanceCtrl::getSpringForce()
{
    Eigen::VectorXd tmp(positionError.size()+orientationError.size());
    tmp<<positionError, -1.0*orientationError;
    return _K*tmp;
}

Eigen::VectorXd CartesianImpedanceCtrl::getDamperForce()
{
    Eigen::VectorXd tmp(linearVelocityError.size()+orientationVelocityError.size());
    tmp<<linearVelocityError, orientationVelocityError;
    return _D*tmp;
}

void CartesianImpedanceCtrl::update_b() {
    cartesian_utils::computeCartesianError(_actualPose, _desiredPose,
                                           positionError, orientationError);



    Eigen::VectorXd qdot(_x_size);
    _robot.getJointVelocity(qdot);
    Eigen::VectorXd xdot = _J*qdot;
    linearVelocityError = _desiredTwist.segment(0,3) - xdot.segment(0,3);
    orientationVelocityError = _desiredTwist.segment(3,3) - xdot.segment(3,3);

    /// TODO: add -Mc*(ddx_d - Jdot*qdot)
    Eigen::VectorXd F = getDamperForce() + getSpringForce();

    _b = _A*_J.transpose()*F;
}

bool CartesianImpedanceCtrl::isCartesianImpedanceCtrl(OpenSoT::Task<Eigen::MatrixXd, Eigen::VectorXd>::TaskPtr task)
{
    return (bool)boost::dynamic_pointer_cast<CartesianImpedanceCtrl>(task);
}

CartesianImpedanceCtrl::Ptr CartesianImpedanceCtrl::asCartesianImpedanceCtrl(OpenSoT::Task<Eigen::MatrixXd, Eigen::VectorXd>::TaskPtr task)
{
    return boost::dynamic_pointer_cast<CartesianImpedanceCtrl>(task);
}

void CartesianImpedanceCtrl::useInertiaMatrix(const bool use)
{
    _use_inertia_matrix = use;
}