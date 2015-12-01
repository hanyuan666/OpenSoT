#include <iCub/iDynTree/yarp_kdl.h>
#include <idynutils/idynutils.h>
#include <idynutils/tests_utils.h>
#include <idynutils/comanutils.h>
#include <gtest/gtest.h>
#include <kdl/frames.hpp>
#include <kdl/framevel.hpp>
#include <kdl/frames_io.hpp>
#include <kdl/utilities/error.h>
#include <OpenSoT/constraints/Aggregated.h>
#include <OpenSoT/constraints/TaskToConstraint.h>
#include <OpenSoT/utils/VelocityAllocation.h>
#include <OpenSoT/tasks/Aggregated.h>
#include <OpenSoT/constraints/velocity/all.h>
#include <OpenSoT/solvers/QPOases.h>
#include <OpenSoT/tasks/velocity/all.h>
#include <OpenSoT/utils/AutoStack.h>
#include <OpenSoT/utils/DefaultHumanoidStack.h>
#include <OpenSoT/SubTask.h>
#include <qpOASES.hpp>
#include <yarp/math/Math.h>
#include <yarp/sig/all.h>
#include <fstream>


using namespace yarp::math;

#define GREEN "\033[0;32m"
#define DEFAULT "\033[0m"
#define TEST_SCA_FILE            "testQPOases_SCA.py"
/* WE WILL TRY SCA PERFORMANCE WHILE TUNING CARTESIAN TASK PARAMETERS, BOUND SCALING, SMOOTHING */
#define TEST_SCA_CT1_FILE   "testQPOases_SCA_CartesianTuning1.py"
#define TEST_SCA_BST_FILE     "testQPOases_SCA_BoundScalingTuning.py"
#define TEST_SCA_DS_FILE     "testQPOases_SCA_DistanceSmoothingTuning.py"
/* for each teas, we want to save:
   Cartesian error (x,y,z,theta,phy,psi)
   2-norm of task error ofr each task in the stack

   we also want to save distance vector for the closest pair of capsules, and the distance

   we won't save (though we will compute) the variance of the Cartesian error - it will be a measure of vibration
   and it will be used as an indicator of the success for the constraint
   */
#define TEST_SCA_CT1_ERRORS_FILE   "testQPOases_SCA_CT1_Errors.eps"
#define TEST_SCA_CT1_DISTANCES_FILE   "testQPOases_SCA_CT1_Distances.eps"
#define TEST_SCA_BST_ERRORS_FILE     "testQPOases_SCA_BST_Errors.eps"
#define TEST_SCA_BST_DISTANCES_FILE     "testQPOases_SCA_BST_Distances.eps"
#define TEST_SCA_DS_ERRORS_FILE "testQPOases_SCA_DS_Errors.eps"
#define TEST_SCA_DS_DISTANCES_FILE "testQPOases_SCA_DS_Distances.eps"
#define dT 25e-3

namespace {

enum SCA_SMOOTHING_STRATEGY { STRATEGY_CARTESIAN_TUNING_1,
                              STRATEGY_BOUNDSCALING_TUNING,
                              STRATEGY_DISTANCE_SMOOTHING };

class testQPOases_SCA:
        public ::testing::Test, 
        public ::testing::WithParamInterface<SCA_SMOOTHING_STRATEGY>
{
protected:
    std::ofstream   _log;

    testQPOases_SCA()
    {
        _log.open(TEST_SCA_FILE);
        _log << "#! /usr/bin/env python" << std::endl
         << std::endl;
        _log << "execfile('" << TEST_SCA_CT1_FILE << "')" << std::endl;
        _log << "execfile('" << TEST_SCA_BST_FILE << "')" << std::endl;
        _log << "execfile('" << TEST_SCA_DS_FILE << "')" << std::endl;
    }

    virtual ~testQPOases_SCA() {
        _log.close();
    }

    virtual void SetUp() {

    }

    virtual void TearDown() {

    }
};


std::list<std::pair<std::string,std::string> > getWalkmanSCAWhiteList() {
    std::list<std::pair<std::string,std::string> > whiteList;

    // lower body - arms collision whitelist for WalkMan (for upper-body manipulation tasks - i.e. not crouching)
    whiteList.push_back(std::pair<std::string,std::string>("LLowLeg","LSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("LHipMot","LSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("RLowLeg","RSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("RHipMot","RSoftHandLink"));

    // torso - arms collision whitelist for WalkMan
    whiteList.push_back(std::pair<std::string,std::string>("DWS","LSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("DWS","LWrMot2"));
    whiteList.push_back(std::pair<std::string,std::string>("DWS","RSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("DWS","RWrMot2"));
    whiteList.push_back(std::pair<std::string,std::string>("TorsoProtections","LElb"));
    whiteList.push_back(std::pair<std::string,std::string>("TorsoProtections","LSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("TorsoProtections","RElb"));
    whiteList.push_back(std::pair<std::string,std::string>("TorsoProtections","RSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("Waist","LSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("Waist","LWrMot2"));
    whiteList.push_back(std::pair<std::string,std::string>("Waist","RSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("Waist","RWrMot2"));

    // arm - am collision whitelist for WalkMan
    whiteList.push_back(std::pair<std::string,std::string>("LShr","RShr"));
    whiteList.push_back(std::pair<std::string,std::string>("LShr","RSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("LShr","RWrMot2"));
    whiteList.push_back(std::pair<std::string,std::string>("LSoftHandLink","RShr"));
    whiteList.push_back(std::pair<std::string,std::string>("LSoftHandLink","RSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("LSoftHandLink","RWrMot2"));
    whiteList.push_back(std::pair<std::string,std::string>("LWrMot2","RShr"));
    whiteList.push_back(std::pair<std::string,std::string>("LWrMot2","RSoftHandLink"));
    whiteList.push_back(std::pair<std::string,std::string>("LWrMot2","RWrMot2"));

    return whiteList;
}

void setupIK(OpenSoT::AutoStack::Ptr& stack,
             OpenSoT::DefaultHumanoidStack& DHS,
             OpenSoT::solvers::QPOases_sot::Ptr& solver,
             iDynUtils &model)
{
    /*                            */
    /*       CREATING STACK       */
    /*                            */

    // defining a stack composed of size two,
    // where the task of first priority is an aggregated of leftLeg and rightLeg,
    // task of priority two is leftArm and rightArm,
    // and the stack is subject to bounds jointLimits and velocityLimits
    stack =
        ( DHS.rightLeg ) /
        ( (DHS.com_XY) << DHS.selfCollisionAvoidance ) /
        ( (DHS.leftArm + DHS.rightArm ) << DHS.selfCollisionAvoidance ) /
        ( (DHS.postural) << DHS.selfCollisionAvoidance );
    stack << DHS.jointLimits; // << DHS.velocityLimits; commented since we are using VelocityALlocation


    /*                            */
    /*      CONFIGURING STACK     */
    /*                            */

    DHS.rightLeg->setLambda(0.6);   DHS.rightLeg->setOrientationErrorGain(1.0);
    DHS.leftLeg->setLambda(0.6);    DHS.leftLeg->setOrientationErrorGain(1.0);
    DHS.rightArm->setLambda(0.1);   DHS.rightArm->setOrientationErrorGain(0.1);
    DHS.leftArm->setLambda(0.1);    DHS.leftArm->setOrientationErrorGain(0.1);
    DHS.comVelocity->setVelocityLimits(yarp::sig::Vector(0.1,3));
    DHS.velocityLimits->setVelocityLimits(0.3);

    yarp::sig::Matrix pW = DHS.postural->getWeight();
    for(unsigned int i_t = 0; i_t < 3; ++i_t)
        pW(model.torso.joint_numbers[i_t],
            model.torso.joint_numbers[i_t]) *= 1e3;
    for(unsigned int i_t = 0; i_t < 6; ++i_t)
    {
        double amt = 7.5e1;
        if(i_t == 3 || i_t == 4)
            amt = 3;
        pW(model.left_leg.joint_numbers[i_t],
           model.left_leg.joint_numbers[i_t]) *= amt;
        pW(model.right_leg.joint_numbers[i_t],
           model.right_leg.joint_numbers[i_t]) *= amt;
    }
    DHS.postural->setWeight(pW);

    std::list<std::pair<std::string,std::string> > whiteList = getWalkmanSCAWhiteList();

    DHS.selfCollisionAvoidance->setCollisionWhiteList(whiteList);

    OpenSoT::VelocityAllocation(stack,
                                dT,
                                0.3,
                                0.6);

    // setting higher velocity limit to last stack --
    // TODO next feature of VelocityAllocation is a last_stack_speed ;)
    typedef std::list<OpenSoT::Constraint<yarp::sig::Matrix,yarp::sig::Vector>::ConstraintPtr>::iterator it_constraint;
    OpenSoT::Task<yarp::sig::Matrix, yarp::sig::Vector>::TaskPtr lastTask = stack->getStack()[3];
    for(it_constraint i_c = lastTask->getConstraints().begin() ;
        i_c != lastTask->getConstraints().end() ; ++i_c) {
        if( boost::dynamic_pointer_cast<
                OpenSoT::constraints::velocity::VelocityLimits>(
                    *i_c))
            boost::dynamic_pointer_cast<
                            OpenSoT::constraints::velocity::VelocityLimits>(
                                *i_c)->setVelocityLimits(.9);
    }

    solver.reset(
        new OpenSoT::solvers::QPOases_sot(stack->getStack(),
                                          stack->getBounds(),
                                          5e10));

}

yarp::sig::Vector getGoodInitialPosition(iDynUtils& model) {
    yarp::sig::Vector q(model.iDyn3_model.getNrOfDOFs(), 0.0);
    yarp::sig::Vector leg(model.left_leg.getNrOfDOFs(), 0.0);
    leg[0] = -25.0 * M_PI/180.0;
    leg[3] =  50.0 * M_PI/180.0;
    leg[5] = -25.0 * M_PI/180.0;
    model.fromRobotToIDyn(leg, q, model.left_leg);
    model.fromRobotToIDyn(leg, q, model.right_leg);
    yarp::sig::Vector arm(model.left_arm.getNrOfDOFs(), 0.0);
    arm[0] = -10.0 * M_PI/180.0;
    arm[1] = 30.0 * M_PI/180.0;
    arm[3] = -80.0 * M_PI/180.0;
    model.fromRobotToIDyn(arm, q, model.left_arm);
    arm[1] = -arm[1];
    model.fromRobotToIDyn(arm, q, model.right_arm);
    return q;
}

//#define TRY_ON_SIMULATOR
// will try script on the simulator, without the prescribed smoothing technique
//#define TRY_NVS

TEST_P(testQPOases_SCA, trySCASmoothing) {

    SCA_SMOOTHING_STRATEGY strategy = GetParam();

#ifdef TRY_ON_SIMULATOR
    yarp::os::Network init;
    WalkmanUtils robot("testSCA");
#endif

    iDynUtils model("walkman",
                    std::string(OPENSOT_TESTS_ROBOTS_DIR)+"walkman/walkman.urdf",
                    std::string(OPENSOT_TESTS_ROBOTS_DIR)+"walkman/walkman.srdf");

    yarp::sig::Vector q = getGoodInitialPosition(model);
    yarp::sig::Vector qns = q;
    model.setFloatingBaseLink(model.left_leg.end_effector_name);
    model.updateiDyn3Model(q, true);

#ifdef TRY_ON_SIMULATOR
    robot.setPositionDirectMode();
    robot.move(q);
    yarp::os::Time::delay(3);
#endif

    OpenSoT::AutoStack::Ptr stack, stackns;
    OpenSoT::solvers::QPOases_sot::Ptr sot, sotns;
    OpenSoT::DefaultHumanoidStack DHS(model, dT, q);
    OpenSoT::DefaultHumanoidStack DHSns(model, dT, qns);

    setupIK(stack, DHS, sot, model); setupIK(stackns, DHSns, sotns, model);

    _log.close();
    if(strategy == STRATEGY_CARTESIAN_TUNING_1)
    {
        _log.open(TEST_SCA_CT1_FILE);
    }
    else if(strategy == STRATEGY_BOUNDSCALING_TUNING)
    {
        _log.open(TEST_SCA_BST_FILE);
        /* @TODO implement */
    }
    else if(strategy == STRATEGY_DISTANCE_SMOOTHING)
    {
        _log.open(TEST_SCA_DS_FILE);
        /* @TODO implement */
    }
    else
    {
        std::cerr << "Error: Unknown Strategy";
        exit(1);
    }


    //SET SOME REFERENCES
    yarp::sig::Matrix actual_pose_y = DHS.leftArm->getActualPose();
    yarp::sig::Matrix desired_pose_y = actual_pose_y;
    desired_pose_y(1,3) = actual_pose_y(1,3) + 0.1;
    desired_pose_y(2,3) = actual_pose_y(2,3) + 0.1;
    yarp::sig::Vector dq(q.size(), 0.0);
    yarp::sig::Vector dqns(q.size(), 0.0);
    double e, enva, epost, epostnva, epost_max, epostnva_max;

    _log << "#! /usr/bin/env python" << std::endl
         << std::endl
         << "import numpy as np" << std::endl
         << "import matplotlib" << std::endl
         << "from matplotlib.pyplot import *" << std::endl;
    _log << "#t, xdot, ydot, zdot, xdotnva, ydotnva, zdotnva,"  // 1-7
         << " q0dot_torso, q1dot_torso, q2dot_torso,"           // 8-10
         << " q0dotnva_torso, q1dotnva_torso, q2dotnva_torso,"  // 11-13
         << " e, e_nva, epost, epost_nva,"                      // 14-17
         << " t_loop, t_loop_nva" << std::endl;                 // 18-19
    _log << "test_data = np.array((";

    double t_loop = 0.0;
    double t_loopnva = 0.0;
    bool settled = false;
    double settling_counter = 1.0;
    bool converged_event = false;

    DHS.leftArm->setReference(desired_pose_y);
    DHSns.leftArm->setReference(desired_pose_y);

#ifdef TRY_ON_SIMULATOR
    double t_test = yarp::os::Time::now();
#else
    double t_test = yarp::os::SystemClock::nowSystem();
#endif
    do
    {
#ifdef TRY_ON_SIMULATOR
        double t_begin = yarp::os::Time::now();
#else
        double t_begin = yarp::os::SystemClock::nowSystem();
#endif

        model.updateiDyn3Model(q, true);
        stack->update(q);

        e = norm(DHS.leftArm->getb());
        /*
        if(useMinimumVelocity) {
            ASSERT_EQ(dq.subVector(model.torso.joint_numbers[0], model.torso.joint_numbers[2]).length(), 3);
            ASSERT_LT(model.torso.joint_numbers[0], model.torso.joint_numbers[2]);
            epost = norm((q-DHS.postural->getReference()).subVector(model.torso.joint_numbers[0], model.torso.joint_numbers[2]));
        } else
            epost = norm(postural->getb());
        if(epost > epost_max)
            epost_max = epost;
        */


        EXPECT_TRUE(sot->solve(dq));
        q += dq;

#ifdef TRY_ON_SIMULATOR
#ifndef TRY_NVS
        robot.move(q);
        yarp::os::Time::delay(0.005);
#endif

        t_loop = yarp::os::Time::now() - t_begin;
#else
        t_loop = yarp::os::SystemClock::nowSystem() - t_begin;
#endif

#ifdef TRY_ON_SIMULATOR
        t_begin = yarp::os::Time::now();
#else
        t_begin = yarp::os::SystemClock::nowSystem();
#endif

        model.updateiDyn3Model(qns, true);
        stackns->update(qns);

        enva = norm(DHSns.leftArm->getb());
        /*
        if(useMinimumVelocity) {
            ASSERT_EQ(qns.subVector(model.torso.joint_numbers[0], model.torso.joint_numbers[2]).length(), 3);
            ASSERT_LT(model.torso.joint_numbers[0], model.torso.joint_numbers[2]);
            epostnva = norm((qns-DHSns.postural->getReference()).subVector(model.torso.joint_numbers[0], model.torso.joint_numbers[2]));
        } else
            epostnva = norm(posturalnva->getb());
        if(epostnva > epostnva_max)
            epostnva_max = epostnva;
        */

        EXPECT_TRUE(sotns->solve(dqns));
        qns+=dqns;

#ifdef TRY_ON_SIMULATOR
#ifdef TRY_NVS
        robot.move(qnva);
        yarp::os::Time::delay(0.005);
#endif

        t_loopnva = yarp::os::Time::now() - t_begin;
#else
        t_loopnva = yarp::os::SystemClock::nowSystem() - t_begin;
#endif


#ifdef TRY_ON_SIMULATOR
        _log << "(" << yarp::os::Time::now() - t_test << ","
#else
        _log << "(" << yarp::os::SystemClock::nowSystem() - t_test << ","
#endif
            << (DHS.leftArm->getA()*dq)[0] << ","
            << (DHS.leftArm->getA()*dq)[1] << ","
            << (DHS.leftArm->getA()*dq)[2] << ","
            << (DHSns.leftArm->getA()*dqns)[0] << ","
            << (DHSns.leftArm->getA()*dqns)[1] << ","
            << (DHSns.leftArm->getA()*dqns)[2] << ","
            << dq[model.torso.joint_numbers[0]]  << ","
            << dq[model.torso.joint_numbers[1]]  << ","
            << dq[model.torso.joint_numbers[2]]  << ","
            << dqns[model.torso.joint_numbers[0]]  << ","
            << dqns[model.torso.joint_numbers[1]]  << ","
            << dqns[model.torso.joint_numbers[2]]  << ","
            << e << "," << enva << "," << epost << "," << epostnva << ","
            << t_loop << "," << t_loopnva << ")," << std::endl;

        if(e < 1.5e-3 && enva < 1.5e-3 && !converged_event)
        {
            converged_event = true;
            std::cout << "settling";
            std::cout.flush();
        }

        if(converged_event)
        {
            settling_counter -= 3e-3;
            std::cout << ".";
            std::cout.flush();
        }

        if(settling_counter <= 0.0)
        {
            settled = true;
            std::cout << std::endl;
            std::cout.flush();
        }

    } while(e > 1.5e-3 || enva > 1.5e-3 || !settled);

    EXPECT_GT(epostnva_max, epost_max)
        << "With velocity allocation, we expect that "
        << "the maximum postural error for a certain task "
        << "will be lower than without velocity allocation.\n"
        << "In fact, having a scaled velocity for the primary "
        << "task means we are willing to invest some resources "
        << "to consistently reduce the error in the lower priority "
        << "tasks, not just when the error of the primary task is "
        << "low. Since the postural task is at a lower priority than "
        << "the cartesian tasks in this test, we want to see the peak "
        << "error for the postural to be higher without VA.";


    _log << "));" << std::endl;

    _log << "se = figure(figsize=(10.27,7.68)); subplot(3,2,1); p = plot(test_data[:,0],test_data[:,(2,3,5,6)]); title('Hand Velocity');" << std::endl;
    _log << "legend(p,('y dot (VA)', 'z dot (VA)', 'y dot (no VA)', 'z dot (no VA)'));" << std::endl;
    _log << "ylabel('Hand Velocity [m/s]'); xlabel('t [s]');" << std::endl;
    _log << "subplot(3,1,2); p = plot(test_data[:,0], np.transpose(np.vstack(((test_data[:,(7,8,9)]**2).sum(1),(test_data[:,(10,11,12)]**2).sum(1))))); title('Torso Joints Velocity');" << std::endl;
    _log << "ylabel('Torso Joint Velocity [rad/s]'); xlabel('t [s]');" << std::endl;
    _log << "legend(p,('Norm of Joint Velocity (VA)', 'Norm of Joint Velocity (no VA)'));" << std::endl;
    _log << "subplot(3,1,3); p = plot(test_data[:,0],test_data[:,(13,14,15,16)]); title('Tracking Error');" << std::endl;
    _log << "legend(p,('Left Hand tracking error (VA)','Left Hand tracking error (no VA)','Postural tracking error (VA)','Postural tracking error (no VA)'));" << std::endl;
    _log << "ylabel('2-norm of task error'); xlabel('t [s]');" << std::endl;

    _log << "et = figure(figsize=(8,6));" << std::endl;

    _log << "subplot(2,2,1); p = plot(test_data[:,0],test_data[:,(17, 18)]);" << std::endl;
    _log << "title('Computation Time');" << std::endl;
    _log << "legend(p,('Smoothing', 'no Smoothing'));" << std::endl;
    _log << "ylabel('Solve Time [s]'); xlabel('t [s]');" << std::endl;

    _log << "subplot(2,2,2); p = plot(test_data[:,0],test_data[:,(17, 18)]);" << std::endl;
    _log << "title('CoM_XY Task Error');" << std::endl;
    _log << "legend(p,('Smoothing', 'no Smoothing'));" << std::endl;
    _log << "ylabel('norm2 of task error'); xlabel('t [s]');" << std::endl;

    _log << "subplot(2,2,3); p = plot(test_data[:,0],test_data[:,(17, 18)]);" << std::endl;
    _log << "title('l_arm + r_arm Task Error');" << std::endl;
    _log << "legend(p,('Smoothing', 'no Smoothing'));" << std::endl;
    _log << "ylabel('norm2 of task error'); xlabel('t [s]');" << std::endl;

    _log << "subplot(2,2,4); p = plot(test_data[:,0],test_data[:,(17, 18)]);" << std::endl;
    _log << "title('Postural Task Error');" << std::endl;
    _log << "legend(p,('Smoothing', 'no Smoothing'));" << std::endl;
    _log << "ylabel('norm2 of task error'); xlabel('t [s]');" << std::endl;

    if(strategy == STRATEGY_CARTESIAN_TUNING_1) {
        _log << "se.savefig('" << TEST_SCA_CT1_DISTANCES_FILE << "', format='eps', transparent=True);" << std::endl;
        _log << "et.savefig('" << TEST_SCA_CT1_ERRORS_FILE << "',format='eps',transparent=True);" << std::endl;
    } else if(strategy == STRATEGY_BOUNDSCALING_TUNING) {
        _log << "se.savefig('" << TEST_SCA_BST_DISTANCES_FILE << "', format='eps', transparent=True);" << std::endl;
        _log << "et.savefig('" << TEST_SCA_BST_ERRORS_FILE << "',format='eps',transparent=True);" << std::endl;
    } else if(strategy == STRATEGY_DISTANCE_SMOOTHING) {
        _log << "se.savefig('" << TEST_SCA_BST_DISTANCES_FILE << "', format='eps', transparent=True);" << std::endl;
        _log << "et.savefig('" << TEST_SCA_BST_ERRORS_FILE << "',format='eps',transparent=True);" << std::endl;
    } else {
        std::cerr << "Unhandlex exception at line " << __LINE__ << std::endl;
        exit(1);
    }
    _log << "show(block=True)" << std::endl;
}

INSTANTIATE_TEST_CASE_P(trySCASmoothingWith_CT_BST,
                        testQPOases_SCA,
                        ::testing::Values(STRATEGY_CARTESIAN_TUNING_1,
                                          STRATEGY_BOUNDSCALING_TUNING));

}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}