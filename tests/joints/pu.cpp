/*************************************************************************
  *                                                                       *
  * Open Dynamics Engine, Copyright (C) 2001,2002 Russell L. Smith.       *
  * All rights reserved.  Email: russ@q12.org   Web: www.q12.org          *
  *                                                                       *
  * This library is free software; you can redistribute it and/or         *
  * modify it under the terms of EITHER:                                  *
  *   (1) The GNU Lesser General Public License as published by the Free  *
  *       Software Foundation; either version 2.1 of the License, or (at  *
  *       your option) any later version. The text of the GNU Lesser      *
  *       General Public License is included with this library in the     *
  *       file LICENSE.TXT.                                               *
  *   (2) The BSD-style license that is included with this library in     *
  *       the file LICENSE-BSD.TXT.                                       *
  *                                                                       *
  * This library is distributed in the hope that it will be useful,       *
  * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the files    *
  * LICENSE.TXT and LICENSE-BSD.TXT for more details.                     *
  *                                                                       *
  *************************************************************************/
//234567890123456789012345678901234567890123456789012345678901234567890123456789
//        1         2         3         4         5         6         7

////////////////////////////////////////////////////////////////////////////////
// This file create unit test for some of the functions found in:
// ode/src/joinst/pu.cpp
//
//
////////////////////////////////////////////////////////////////////////////////

#include <UnitTest++.h>
#include <ode/ode.h>

#include "../../ode/src/joints/pu.h"

SUITE (TestdxJointPU)
{
    // The 2 bodies are positionned at (0, 0, 0),  and (0, 0, 0)
    // The second body has a rotation of 27deg around X axis.
    // The joint is a PU Joint
    // Axis is along the X axis
    // Anchor at (0, 0, 0)
    struct Fixture_dxJointPU_B1_and_B2_At_Zero_Axis_Along_X
    {
        Fixture_dxJointPU_B1_and_B2_At_Zero_Axis_Along_X()
        {
            wId = dWorldCreate();

            bId1 = dBodyCreate (wId);
            dBodySetPosition (bId1, 0, 0, 0);

            bId2 = dBodyCreate (wId);
            dBodySetPosition (bId2, 0, 0, 0);

            dMatrix3 R;

            dRFromAxisAndAngle (R, 1, 0, 0, REAL(0.47123)); // 27deg
            dBodySetRotation (bId2, R);

            jId   = dJointCreatePU (wId, 0);
            joint = (dxJointPU*) jId;


            dJointAttach (jId, bId1, bId2);
        }

        ~Fixture_dxJointPU_B1_and_B2_At_Zero_Axis_Along_X()
        {
            dWorldDestroy (wId);
        }

        dWorldID wId;

        dBodyID bId1;
        dBodyID bId2;


        dJointID jId;
        dxJointPU* joint;
    };

    // Test is dJointSetPUAxis and dJointGetPUAxis return same value
    TEST_FIXTURE (Fixture_dxJointPU_B1_and_B2_At_Zero_Axis_Along_X,
                  test_dJointSetGetPUAxis)
    {
        dVector3 axisOrig, axis;


        dJointGetPUAxis1 (jId, axisOrig);
        dJointGetPUAxis1 (jId, axis);
        dJointSetPUAxis1 (jId, axis[0], axis[1], axis[2]);
        dJointGetPUAxis1 (jId, axis);
        CHECK_CLOSE (axis[0], axisOrig[0] , 1e-4);
        CHECK_CLOSE (axis[1], axisOrig[1] , 1e-4);
        CHECK_CLOSE (axis[2], axisOrig[2] , 1e-4);


        dJointGetPUAxis2 (jId, axisOrig);
        dJointGetPUAxis2(jId, axis);
        dJointSetPUAxis2 (jId, axis[0], axis[1], axis[2]);
        dJointGetPUAxis2 (jId, axis);
        CHECK_CLOSE (axis[0], axisOrig[0] , 1e-4);
        CHECK_CLOSE (axis[1], axisOrig[1] , 1e-4);
        CHECK_CLOSE (axis[2], axisOrig[2] , 1e-4);


        dJointGetPUAxis3 (jId, axisOrig);
        dJointGetPUAxis3(jId, axis);
        dJointSetPUAxis3 (jId, axis[0], axis[1], axis[2]);
        dJointGetPUAxis3 (jId, axis);
        CHECK_CLOSE (axis[0], axisOrig[0] , 1e-4);
        CHECK_CLOSE (axis[1], axisOrig[1] , 1e-4);
        CHECK_CLOSE (axis[2], axisOrig[2] , 1e-4);
    }

  // Create 2 bodies attached by a PU joint
  // Axis is along the X axis (Default value
  // Anchor at (0, 0, 0)      (Default value)
  //
  //       ^Y
  //       |
  //       * Body2
  //       |
  //       |
  // Body1 |
  // *     Z-------->
  struct dxJointPU_Test_Initialization {
    dxJointPU_Test_Initialization()
    {
      wId = dWorldCreate();

      // Remove gravity to have the only force be the force of the joint
      dWorldSetGravity(wId, 0,0,0);

      for (int j=0; j<2; ++j) {
        bId[j][0] = dBodyCreate (wId);
        dBodySetPosition (bId[j][0], -1, 0, 0);

        bId[j][1] = dBodyCreate (wId);
        dBodySetPosition (bId[j][1], 1, 0, 0);

        dMatrix3 R;

        dRFromAxisAndAngle (R, 1, 0, 0, REAL(1.57));
        dBodySetRotation (bId[j][1], R);

        jId[j]   = dJointCreatePU (wId, 0);
        dJointAttach (jId[j], bId[j][0], bId[j][1]);
        dJointSetPUParam(jId[j], dParamLoStop, 1);
        dJointSetPUParam(jId[j], dParamHiStop, 2);
        dJointSetPUParam(jId[j], dParamFMax, 200);
      }
    }

    ~dxJointPU_Test_Initialization()
    {
      dWorldDestroy (wId);
    }

    dWorldID wId;

    dBodyID bId[2][2];


    dJointID jId[2];

  };


  // Test if setting a PU with its default values
  // will behave the same as a default PU joint
  TEST_FIXTURE (dxJointPU_Test_Initialization,
                test_PU_Initialization) {
    using namespace std;

    dVector3 axis;
    dJointGetPUAxis1(jId[1], axis);
    dJointSetPUAxis1(jId[1], axis[0], axis[1], axis[2]);

    dJointGetPUAxis2(jId[1], axis);
    dJointSetPUAxis2(jId[1], axis[0], axis[1], axis[2]);

    dJointGetPUAxis3(jId[1], axis);
    dJointSetPUAxis3(jId[1], axis[0], axis[1], axis[2]);



    dVector3 anchor;
    dJointGetPUAnchor(jId[1], anchor);
    dJointSetPUAnchor(jId[1], anchor[0], anchor[1], anchor[2]);


    for (int b=0; b<2; ++b) {
      // Compare body b of the first joint with its equivalent on the
      // second joint
      const dReal *qA = dBodyGetQuaternion(bId[0][b]);
      const dReal *qB = dBodyGetQuaternion(bId[1][b]);
      CHECK_CLOSE (qA[0], qB[0], 1e-6);
      CHECK_CLOSE (qA[1], qB[1], 1e-6);
      CHECK_CLOSE (qA[2], qB[2], 1e-6);
      CHECK_CLOSE (qA[3], qB[3], 1e-6);
    }

    dWorldStep (wId,0.5);
    dWorldStep (wId,0.5);
    dWorldStep (wId,0.5);
    dWorldStep (wId,0.5);

    for (int b=0; b<2; ++b) {
      // Compare body b of the first joint with its equivalent on the
      // second joint
      const dReal *qA = dBodyGetQuaternion(bId[0][b]);
      const dReal *qB = dBodyGetQuaternion(bId[1][b]);
      CHECK_CLOSE (qA[0], qB[0], 1e-6);
      CHECK_CLOSE (qA[1], qB[1], 1e-6);
      CHECK_CLOSE (qA[2], qB[2], 1e-6);
      CHECK_CLOSE (qA[3], qB[3], 1e-6);


      const dReal *posA = dBodyGetPosition(bId[0][b]);
      const dReal *posB = dBodyGetPosition(bId[1][b]);
      CHECK_CLOSE (posA[0], posB[0], 1e-6);
      CHECK_CLOSE (posA[1], posB[1], 1e-6);
      CHECK_CLOSE (posA[2], posB[2], 1e-6);
      CHECK_CLOSE (posA[3], posB[3], 1e-6);
    }

  }

} // End of SUITE TestdxJointPU

