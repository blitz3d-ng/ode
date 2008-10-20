/*************************************************************************
 *                                                                       *
 * Open Dynamics Engine, Copyright (C) 2001-2003 Russell L. Smith.       *
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
/*
Code for Convex Collision Detection
By Rodrigo Hernandez
*/
//#include <algorithm>
#include <ode/common.h>
#include <ode/collision.h>
#include <ode/matrix.h>
#include <ode/rotation.h>
#include <ode/odemath.h>
#include "collision_kernel.h"
#include "collision_std.h"
#include "collision_util.h"

#ifdef _MSC_VER
#pragma warning(disable:4291)  // for VC++, no complaints about "no matching operator delete found"
#endif

#if _MSC_VER <= 1200
#define dMIN(A,B)  ((A)>(B) ? (B) : (A))
#define dMAX(A,B)  ((A)>(B) ? (A) : (B))
#else
#define dMIN(A,B)  std::min(A,B)
#define dMAX(A,B)  std::max(A,B)
#endif

//****************************************************************************
// Convex public API
dxConvex::dxConvex (dSpaceID space,
		    dReal *_planes,
		    unsigned int _planecount,
		    dReal *_points,
		    unsigned int _pointcount,
		    unsigned int *_polygons) : 
  dxGeom (space,1)
{
  dAASSERT (_planes != NULL);
  dAASSERT (_points != NULL);
  dAASSERT (_polygons != NULL);
  //fprintf(stdout,"dxConvex Constructor planes %X\n",_planes);
  type = dConvexClass;
  planes = _planes;
  planecount = _planecount;
  // we need points as well
  points = _points;
  pointcount = _pointcount;
  polygons=_polygons;
  FillEdges();
}


void dxConvex::computeAABB()
{
  // this can, and should be optimized
  dVector3 point;
  dMULTIPLY0_331 (point,final_posr->R,points);
  aabb[0] = point[0]+final_posr->pos[0];
  aabb[1] = point[0]+final_posr->pos[0];
  aabb[2] = point[1]+final_posr->pos[1];
  aabb[3] = point[1]+final_posr->pos[1];
  aabb[4] = point[2]+final_posr->pos[2];
  aabb[5] = point[2]+final_posr->pos[2];
  for(unsigned int i=3;i<(pointcount*3);i+=3)
    {
      dMULTIPLY0_331 (point,final_posr->R,&points[i]);
      aabb[0] = dMIN(aabb[0],point[0]+final_posr->pos[0]);
      aabb[1] = dMAX(aabb[1],point[0]+final_posr->pos[0]);
      aabb[2] = dMIN(aabb[2],point[1]+final_posr->pos[1]);
      aabb[3] = dMAX(aabb[3],point[1]+final_posr->pos[1]);
      aabb[4] = dMIN(aabb[4],point[2]+final_posr->pos[2]);
      aabb[5] = dMAX(aabb[5],point[2]+final_posr->pos[2]);
    }
}

/*! \brief Populates the edges set, should be called only once whenever
  the polygon array gets updated */
void dxConvex::FillEdges()
{
	unsigned int *points_in_poly=polygons;
	unsigned int *index=polygons+1;
	for(unsigned int i=0;i<planecount;++i)
	{
		//fprintf(stdout,"Points in Poly: %d\n",*points_in_poly);
		for(unsigned int j=0;j<*points_in_poly;++j)
		{
			edges.insert(edge(dMIN(index[j],index[(j+1)%*points_in_poly]),
				dMAX(index[j],index[(j+1)%*points_in_poly])));
			//fprintf(stdout,"Insert %d-%d\n",index[j],index[(j+1)%*points_in_poly]);
		}
		points_in_poly+=(*points_in_poly+1);
		index=points_in_poly+1;
	}
	/*
	fprintf(stdout,"Edge Count: %d\n",edges.size());
	for(std::set<edge>::iterator it=edges.begin();
	it!=edges.end();
	++it)
	{
	fprintf(stdout,"Edge: %d-%d\n",it->first,it->second);
	}
	*/
}

dGeomID dCreateConvex (dSpaceID space,dReal *_planes,unsigned int _planecount,
		    dReal *_points,
		       unsigned int _pointcount,
		       unsigned int *_polygons)
{
  //fprintf(stdout,"dxConvex dCreateConvex\n");
  return new dxConvex(space,_planes, _planecount,
		      _points,
		      _pointcount,
		      _polygons);
}

void dGeomSetConvex (dGeomID g,dReal *_planes,unsigned int _planecount,
		     dReal *_points,
		     unsigned int _pointcount,
		     unsigned int *_polygons)
{
  //fprintf(stdout,"dxConvex dGeomSetConvex\n");
  dUASSERT (g && g->type == dConvexClass,"argument not a convex shape");
  dxConvex *s = (dxConvex*) g;
  s->planes = _planes;
  s->planecount = _planecount;
  s->points = _points;
  s->pointcount = _pointcount;
  s->polygons=_polygons;
}

//****************************************************************************
// Helper Inlines
//

/*! \brief Returns Whether or not the segment ab intersects plane p
  \param a origin of the segment
  \param b segment destination
  \param p plane to test for intersection
  \param t returns the time "t" in the segment ray that gives us the intersecting 
  point
  \param q returns the intersection point
  \return true if there is an intersection, otherwise false.
*/
bool IntersectSegmentPlane(dVector3 a, 
			   dVector3 b, 
			   dVector4 p, 
			   dReal &t, 
			   dVector3 q)
{
  // Compute the t value for the directed line ab intersecting the plane
  dVector3 ab;
  ab[0]= b[0] - a[0];
  ab[1]= b[1] - a[1];
  ab[2]= b[2] - a[2];
  
  t = (p[3] - dDOT(p,a)) / dDOT(p,ab);
  
  // If t in [0..1] compute and return intersection point
  if (t >= 0.0 && t <= 1.0) 
    {
      q[0] = a[0] + t * ab[0];
      q[1] = a[1] + t * ab[1];
      q[2] = a[2] + t * ab[2];
      return true;
    }
  // Else no intersection
  return false;
}

/*! \brief Returns the Closest Point in Ray 1 to Ray 2
  \param Origin1 The origin of Ray 1
  \param Direction1 The direction of Ray 1
  \param Origin1 The origin of Ray 2
  \param Direction1 The direction of Ray 3
  \param t the time "t" in Ray 1 that gives us the closest point 
  (closest_point=Origin1+(Direction*t).
  \return true if there is a closest point, false if the rays are paralell.
*/
inline bool ClosestPointInRay(const dVector3 Origin1,
			      const dVector3 Direction1,
			      const dVector3 Origin2,
			      const dVector3 Direction2,
			      dReal& t)
{
  dVector3 w = {Origin1[0]-Origin2[0],
		Origin1[1]-Origin2[1],
		Origin1[2]-Origin2[2]};
  dReal a = dDOT(Direction1 , Direction1);
  dReal b = dDOT(Direction1 , Direction2);
  dReal c = dDOT(Direction2 , Direction2);
  dReal d = dDOT(Direction1 , w);
  dReal e = dDOT(Direction2 , w);
  dReal denominator = (a*c)-(b*b);
  if(denominator==0.0f)
    {
      return false;
    }
  t = ((a*e)-(b*d))/denominator;
  return true;
}

/*! \brief Returns the Ray on which 2 planes intersect if they do.
  \param p1 Plane 1
  \param p2 Plane 2
  \param p Contains the origin of the ray upon returning if planes intersect
  \param d Contains the direction of the ray upon returning if planes intersect
  \return true if the planes intersect, false if paralell.
*/
inline bool IntersectPlanes(const dVector4 p1, const dVector4 p2, dVector3 p, dVector3 d)
{
  // Compute direction of intersection line
  //Cross(p1, p2,d);
  dCROSS(d,=,p1,p2);
  
  // If d is (near) zero, the planes are parallel (and separated)
  // or coincident, so they're not considered intersecting
  dReal denom = dDOT(d, d);
  if (denom < dEpsilon) return false;

  dVector3 n;
  n[0]=p1[3]*p2[0] - p2[3]*p1[0];
  n[1]=p1[3]*p2[1] - p2[3]*p1[1];
  n[2]=p1[3]*p2[2] - p2[3]*p1[2];
  // Compute point on intersection line
  //Cross(n, d,p);
  dCROSS(p,=,n,d);
  p[0]/=denom;
  p[1]/=denom;
  p[2]/=denom;
  return true;
}

/*! \brief Finds out if a point lies inside a 2D polygon
  \param p Point to test
  \param polygon a pointer to the start of the convex polygon index buffer
  \param out the closest point in the polygon if the point is not inside
  \return true if the point lies inside of the polygon, false if not.
*/
inline bool IsPointInPolygon(dVector3 p,
			     unsigned int *polygon,
			     dxConvex *convex,
			     dVector3 out)
{
  // p is the point we want to check,
  // polygon is a pointer to the polygon we
  // are checking against, remember it goes
  // number of vertices then that many indexes
  // out returns the closest point on the border of the
  // polygon if the point is not inside it.
  size_t pointcount=polygon[0];
  dVector3 a;
  dVector3 b;
  dVector3 c;
  dVector3 ab;
  dVector3 ac;
  dVector3 ap;
  dVector3 bp;
  dReal d1;
  dReal d2;
  dReal d3;
  dReal d4;
  dReal vc;
  polygon++; // skip past pointcount
  for(size_t i=0;i<pointcount;++i)
    {
      dMULTIPLY0_331 (a,convex->final_posr->R,&convex->points[(polygon[i]*3)]);
      a[0]=convex->final_posr->pos[0]+a[0];
      a[1]=convex->final_posr->pos[1]+a[1];
      a[2]=convex->final_posr->pos[2]+a[2];

      dMULTIPLY0_331 (b,convex->final_posr->R,
		      &convex->points[(polygon[(i+1)%pointcount]*3)]);
      b[0]=convex->final_posr->pos[0]+b[0];
      b[1]=convex->final_posr->pos[1]+b[1];
      b[2]=convex->final_posr->pos[2]+b[2];

      dMULTIPLY0_331 (c,convex->final_posr->R,
		      &convex->points[(polygon[(i+2)%pointcount]*3)]);
      c[0]=convex->final_posr->pos[0]+c[0];
      c[1]=convex->final_posr->pos[1]+c[1];
      c[2]=convex->final_posr->pos[2]+c[2];

      ab[0] = b[0] - a[0];
      ab[1] = b[1] - a[1];
      ab[2] = b[2] - a[2];
      ac[0] = c[0] - a[0];
      ac[1] = c[1] - a[1];
      ac[2] = c[2] - a[2];
      ap[0] = p[0] - a[0];
      ap[1] = p[1] - a[1];
      ap[2] = p[2] - a[2];
      d1 = dDOT(ab,ap);
      d2 = dDOT(ac,ap);
      if (d1 <= 0.0f && d2 <= 0.0f)
	{
	  out[0]=a[0];
	  out[1]=a[1];
	  out[2]=a[2];
	  return false;
	}
      bp[0] = p[0] - b[0];
      bp[1] = p[1] - b[1];
      bp[2] = p[2] - b[2];
      d3 = dDOT(ab,bp);
      d4 = dDOT(ac,bp);
      if (d3 >= 0.0f && d4 <= d3)
	{
	  out[0]=b[0];
	  out[1]=b[1];
	  out[2]=b[2];
	  return false;
	}      
      vc = d1*d4 - d3*d2;
      if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) 
	{
	  dReal v = d1 / (d1 - d3);
	  out[0] = a[0] + (ab[0]*v);
	  out[1] = a[1] + (ab[1]*v);
	  out[2] = a[2] + (ab[2]*v);
	  return false;
	}
    }
  return true;
}

int dCollideConvexPlane (dxGeom *o1, dxGeom *o2, int flags,
						 dContactGeom *contact, int skip)
{
	dIASSERT (skip >= (int)sizeof(dContactGeom));
	dIASSERT (o1->type == dConvexClass);
	dIASSERT (o2->type == dPlaneClass);
	dIASSERT ((flags & NUMC_MASK) >= 1);
	
	dxConvex *Convex = (dxConvex*) o1;
	dxPlane *Plane = (dxPlane*) o2;
	unsigned int contacts=0;
	unsigned int maxc = flags & NUMC_MASK;
	dVector3 v2;

#define LTEQ_ZERO	0x10000000
#define GTEQ_ZERO	0x20000000
#define BOTH_SIGNS	(LTEQ_ZERO | GTEQ_ZERO)
	dIASSERT((BOTH_SIGNS & NUMC_MASK) == 0); // used in conditional operator later

	unsigned int totalsign = 0;
	for(unsigned int i=0;i<Convex->pointcount;++i)
	{
		dMULTIPLY0_331 (v2,Convex->final_posr->R,&Convex->points[(i*3)]);
		dVector3Add(Convex->final_posr->pos, v2, v2);
		
		unsigned int distance2sign = GTEQ_ZERO;
		dReal distance2 = dVector3Dot(Plane->p, v2) - Plane->p[3]; // Ax + By + Cz - D
		if((distance2 <= REAL(0.0)))
		{
			distance2sign = distance2 != REAL(0.0) ? LTEQ_ZERO : BOTH_SIGNS;

			if (contacts != maxc)
			{
				dContactGeom *target = SAFECONTACT(flags, contact, contacts, skip);
				dVector3Copy(Plane->p, target->normal);
				dVector3Copy(v2, target->pos);
				target->depth = -distance2;
				target->g1 = Convex;
				target->g2 = Plane;
				contacts++;
			}
		}

		// Take new sign into account
		totalsign |= distance2sign;
		// Check if contacts are full and both signs have been already found
		if ((contacts ^ maxc | totalsign) == BOTH_SIGNS) // harder to comprehend but requires one register less
		{
			break; // Nothing can be changed any more
		}
	}
	if (totalsign == BOTH_SIGNS) return contacts;
	return 0;
#undef BOTH_SIGNS
#undef GTEQ_ZERO
#undef LTEQ_ZERO
}

int dCollideSphereConvex (dxGeom *o1, dxGeom *o2, int flags,
			  dContactGeom *contact, int skip)
{
  dIASSERT (skip >= (int)sizeof(dContactGeom));
  dIASSERT (o1->type == dSphereClass);
  dIASSERT (o2->type == dConvexClass);
  dIASSERT ((flags & NUMC_MASK) >= 1);

  dxSphere *Sphere = (dxSphere*) o1;
  dxConvex *Convex = (dxConvex*) o2;
  dReal dist,closestdist=dInfinity;
  dVector4 plane;
  // dVector3 contactpoint;
  dVector3 offsetpos,out,temp;
  unsigned int *pPoly=Convex->polygons;
  int closestplane;
  bool sphereinside=true;
  /* 
     Do a good old sphere vs plane check first,
     if a collision is found then check if the contact point
     is within the polygon
  */
  // offset the sphere final_posr->position into the convex space
  offsetpos[0]=Sphere->final_posr->pos[0]-Convex->final_posr->pos[0];
  offsetpos[1]=Sphere->final_posr->pos[1]-Convex->final_posr->pos[1];
  offsetpos[2]=Sphere->final_posr->pos[2]-Convex->final_posr->pos[2];
  //fprintf(stdout,"Begin Check\n");  
  for(unsigned int i=0;i<Convex->planecount;++i)
    {
      // apply rotation to the plane
      dMULTIPLY0_331(plane,Convex->final_posr->R,&Convex->planes[(i*4)]);
      plane[3]=(&Convex->planes[(i*4)])[3];
      // Get the distance from the sphere origin to the plane
      dist = dVector3Dot(plane, offsetpos) - plane[3]; // Ax + By + Cz - D
      if(dist>0)
	{
	  // if we get here, we know the center of the sphere is
	  // outside of the convex hull.
	  if(dist<Sphere->radius)
	    {
	      // if we get here we know the sphere surface penetrates
	      // the plane
	      if(IsPointInPolygon(Sphere->final_posr->pos,pPoly,Convex,out))
		{
		  // finally if we get here we know that the
		  // sphere is directly touching the inside of the polyhedron
		  //fprintf(stdout,"Contact! distance=%f\n",dist);
		  contact->normal[0] = plane[0];
		  contact->normal[1] = plane[1];
		  contact->normal[2] = plane[2];
		  contact->pos[0] = Sphere->final_posr->pos[0]+
		    (-contact->normal[0]*Sphere->radius);
		  contact->pos[1] = Sphere->final_posr->pos[1]+
		    (-contact->normal[1]*Sphere->radius);
		  contact->pos[2] = Sphere->final_posr->pos[2]+
		    (-contact->normal[2]*Sphere->radius);
		  contact->depth = Sphere->radius-dist;
		  contact->g1 = Sphere;
		  contact->g2 = Convex;
		  return 1;
		}
	      else
		{
		  // the sphere may not be directly touching
		  // the polyhedron, but it may be touching
		  // a point or an edge, if the distance between
		  // the closest point on the poly (out) and the 
		  // center of the sphere is less than the sphere 
		  // radius we have a hit.
		  temp[0] = (Sphere->final_posr->pos[0]-out[0]);
		  temp[1] = (Sphere->final_posr->pos[1]-out[1]);
		  temp[2] = (Sphere->final_posr->pos[2]-out[2]);
		  dist=(temp[0]*temp[0])+(temp[1]*temp[1])+(temp[2]*temp[2]);
		  // avoid the sqrt unless really necesary
		  if(dist<(Sphere->radius*Sphere->radius))
		    {
		      // We got an indirect hit
		      dist=dSqrt(dist);
		      contact->normal[0] = temp[0]/dist;
		      contact->normal[1] = temp[1]/dist;
		      contact->normal[2] = temp[2]/dist;
		      contact->pos[0] = Sphere->final_posr->pos[0]+
			(-contact->normal[0]*Sphere->radius);
		      contact->pos[1] = Sphere->final_posr->pos[1]+
			(-contact->normal[1]*Sphere->radius);
		      contact->pos[2] = Sphere->final_posr->pos[2]+
			(-contact->normal[2]*Sphere->radius);
		      contact->depth = Sphere->radius-dist;
		      contact->g1 = Sphere;
		      contact->g2 = Convex;
		      return 1;
		    }
		}
	    }
	  sphereinside=false;
	}
      if(sphereinside)
	{
	  if(closestdist>dFabs(dist))
	    {
	      closestdist=dFabs(dist);
	      closestplane=i;
	    }
	}
      pPoly+=pPoly[0]+1;
    }
    if(sphereinside)
      {
	// if the center of the sphere is inside 
	// the Convex, we need to pop it out
	dMULTIPLY0_331(contact->normal,
		       Convex->final_posr->R,
		       &Convex->planes[(closestplane*4)]);
	contact->pos[0] = Sphere->final_posr->pos[0];
	contact->pos[1] = Sphere->final_posr->pos[1];
	contact->pos[2] = Sphere->final_posr->pos[2];
	contact->depth = closestdist+Sphere->radius;
	contact->g1 = Sphere;
	contact->g2 = Convex;
	return 1;
      }
  return 0;
}

int dCollideConvexBox (dxGeom *o1, dxGeom *o2, int flags,
		       dContactGeom *contact, int skip)
{
  dIASSERT (skip >= (int)sizeof(dContactGeom));
  dIASSERT (o1->type == dConvexClass);
  dIASSERT (o2->type == dBoxClass);
  dIASSERT ((flags & NUMC_MASK) >= 1);
  
  //dxConvex *Convex = (dxConvex*) o1;
  //dxBox *Box = (dxBox*) o2;
  
  return 0;
}

int dCollideConvexCapsule (dxGeom *o1, dxGeom *o2,
			     int flags, dContactGeom *contact, int skip)
{
  dIASSERT (skip >= (int)sizeof(dContactGeom));
  dIASSERT (o1->type == dConvexClass);
  dIASSERT (o2->type == dCapsuleClass);
  dIASSERT ((flags & NUMC_MASK) >= 1);

  //dxConvex *Convex = (dxConvex*) o1;
  //dxCapsule *Capsule = (dxCapsule*) o2;
  
  return 0;
}

/*! \brief A Support mapping function for convex shapes
  \param dir direction to find the Support Point for
  \param cvx convex object to find the support point for
  \param out the support mapping in dir.
 */
inline void Support(dVector3 dir,dxConvex& cvx,dVector3 out)
{
	dVector3 point;
	dMULTIPLY0_331 (point,cvx.final_posr->R,cvx.points);
	point[0]+=cvx.final_posr->pos[0];
	point[1]+=cvx.final_posr->pos[1];
	point[2]+=cvx.final_posr->pos[2];

	dReal max = dDOT(point,dir);
	dReal tmp;
	for (unsigned int i = 1; i < cvx.pointcount; ++i) 
	{
		dMULTIPLY0_331 (point,cvx.final_posr->R,cvx.points+(i*3));
		point[0]+=cvx.final_posr->pos[0];
		point[1]+=cvx.final_posr->pos[1];
		point[2]+=cvx.final_posr->pos[2];      
		tmp = dDOT(point, dir);
		if (tmp > max) 
		{ 
			out[0]=point[0];
			out[1]=point[1];
			out[2]=point[2];
			max = tmp; 
		}
	}
}

inline void ComputeInterval(dxConvex& cvx,dVector4 axis,dReal& min,dReal& max)
{
  /* TODO: Use Support points here */
  dVector3 point;
  dReal value;
  //fprintf(stdout,"Compute Interval Axis %f,%f,%f\n",axis[0],axis[1],axis[2]);
  dMULTIPLY0_331(point,cvx.final_posr->R,cvx.points);
  //fprintf(stdout,"initial point %f,%f,%f\n",point[0],point[1],point[2]);
  point[0]+=cvx.final_posr->pos[0];
  point[1]+=cvx.final_posr->pos[1];
  point[2]+=cvx.final_posr->pos[2];
  max = min = dDOT(point,axis)-axis[3];//(*)
  for (unsigned int i = 1; i < cvx.pointcount; ++i) 
    {
      dMULTIPLY0_331 (point,cvx.final_posr->R,cvx.points+(i*3));
      point[0]+=cvx.final_posr->pos[0];
      point[1]+=cvx.final_posr->pos[1];
      point[2]+=cvx.final_posr->pos[2];
      value=dDOT(point,axis)-axis[3];//(*)
      if(value<min)
	{
	  min=value;
	}
      else if(value>max)
	{
	  max=value;
	}
    }
  // *: usually using the distance part of the plane (axis) is
  // not necesary, however, here we need it here in order to know
  // which face to pick when there are 2 parallel sides.
}

bool CheckEdgeIntersection(dxConvex& cvx1,dxConvex& cvx2, int flags,int& curc,
			   dContactGeom *contact, int skip)
{
  int maxc = flags & NUMC_MASK;
  dIASSERT(maxc != 0);
  dVector3 e1,e2,q;
  dVector4 plane,depthplane;
  dReal t;
  for(std::set<edge>::iterator i = cvx1.edges.begin();
      i!= cvx1.edges.end();
      ++i)
    {
      // Rotate
      dMULTIPLY0_331(e1,cvx1.final_posr->R,cvx1.points+(i->first*3));
      // translate
      e1[0]+=cvx1.final_posr->pos[0];
      e1[1]+=cvx1.final_posr->pos[1];
      e1[2]+=cvx1.final_posr->pos[2];
      // Rotate
      dMULTIPLY0_331(e2,cvx1.final_posr->R,cvx1.points+(i->second*3));
      // translate
      e2[0]+=cvx1.final_posr->pos[0];
      e2[1]+=cvx1.final_posr->pos[1];
      e2[2]+=cvx1.final_posr->pos[2];
      unsigned int* pPoly=cvx2.polygons;
      for(size_t j=0;j<cvx2.planecount;++j)
	{
	  // Rotate
	  dMULTIPLY0_331(plane,cvx2.final_posr->R,cvx2.planes+(j*4));
	  dNormalize3(plane);
	  // Translate
	  plane[3]=
	    (cvx2.planes[(j*4)+3])+
	    ((plane[0] * cvx2.final_posr->pos[0]) + 
	     (plane[1] * cvx2.final_posr->pos[1]) + 
	     (plane[2] * cvx2.final_posr->pos[2]));
	  dContactGeom *target = SAFECONTACT(flags, contact, curc, skip);
	  target->g1=&cvx1; // g1 is the one pushed
	  target->g2=&cvx2;
	  if(IntersectSegmentPlane(e1,e2,plane,t,target->pos))
	    {
	      if(IsPointInPolygon(target->pos,pPoly,&cvx2,q))
		{
		  target->depth = dInfinity;
		  for(size_t k=0;k<cvx2.planecount;++k)
		    {
		      if(k==j) continue; // we're already at 0 depth on this plane
		      // Rotate
		      dMULTIPLY0_331(depthplane,cvx2.final_posr->R,cvx2.planes+(k*4));
		      dNormalize3(depthplane);
		      // Translate
		      depthplane[3]=
			(cvx2.planes[(k*4)+3])+
			((plane[0] * cvx2.final_posr->pos[0]) + 
			 (plane[1] * cvx2.final_posr->pos[1]) + 
			 (plane[2] * cvx2.final_posr->pos[2]));
		      dReal depth = (dVector3Dot(depthplane, target->pos) - depthplane[3]); // Ax + By + Cz - D
		      if((fabs(depth)<fabs(target->depth))&&((depth<-dEpsilon)||(depth>dEpsilon)))
			{
			  target->depth=depth;
			  dVector3Copy(depthplane,target->normal);
			}
		    }
		  ++curc;
		  if(curc==maxc)
		    return true;
		}
	    }
	  pPoly+=pPoly[0]+1;
	}
    }
  return false;
}
/*! \brief Does an axis separation test using cvx1 planes on cvx1 and cvx2, returns true for a collision false for no collision 
  \param cvx1 [IN] First Convex object, its planes are used to do the tests
  \param cvx2 [IN] Second Convex object
  \param min_depth [IN/OUT] Used to input as well as output the minimum depth so far, must be set to a huge value such as dInfinity for initialization.
  \param g1 [OUT] Pointer to the convex which should be used in the returned contact as g1
  \param g2 [OUT] Pointer to the convex which should be used in the returned contact as g2
 */
inline bool CheckSATConvexFaces(dxConvex& cvx1,dxConvex& cvx2,dReal& min_depth,int& side_index,dxConvex** g1,dxConvex** g2)
{
  dReal min,max,min1,max1,min2,max2,depth;
  dVector4 plane;
  for(unsigned int i=0;i<cvx1.planecount;++i)
    {
      // -- Apply Transforms --
      // Rotate
      dMULTIPLY0_331(plane,cvx1.final_posr->R,cvx1.planes+(i*4));
      dNormalize3(plane);
      // Translate
      plane[3]=
	(cvx1.planes[(i*4)+3])+
	((plane[0] * cvx1.final_posr->pos[0]) + 
	 (plane[1] * cvx1.final_posr->pos[1]) + 
	 (plane[2] * cvx1.final_posr->pos[2]));
      ComputeInterval(cvx1,plane,min1,max1);
      ComputeInterval(cvx2,plane,min2,max2);
      if(max2<min1 || max1<min2) return false;
      min = std::max(min1, min2);
      max = std::min(max1, max2);
      depth = max-min;
      /* 
	 Take only into account the faces that penetrate cvx1 to determine
	 minimum depth
	 ((max2*min2)<0) = different sign
      */
      if (((max2*min2)<0) && (dFabs(depth)<dFabs(min_depth)))
	{
	  min_depth=depth;
	  // This is wrong if there are 2 parallel sides (Working on it)
	  side_index=(int)i;
	  *g1=&cvx1;
	  *g2=&cvx2;
	  //printf("Depth %f Min Max %f %f\n",depth,min2,max2);
	}
    }
  return true;
}
/*! \brief Does an axis separation test using cvx1 and cvx2 edges, returns true for a collision false for no collision 
  \param cvx1 [IN] First Convex object
  \param cvx2 [IN] Second Convex object
  \param min_depth [IN/OUT] Used to input as well as output the minimum depth so far, must be set to a huge value such as dInfinity for initialization.
  \param g1 [OUT] Pointer to the convex which should be used in the returned contact as g1
  \param g2 [OUT] Pointer to the convex which should be used in the returned contact as g2
 */
inline bool CheckSATConvexEdges(dxConvex& cvx1,
				dxConvex& cvx2,
				dReal& min_depth,
				int& side_index,
				dxConvex** g1,
				dxConvex** g2)
{
  /*
    This function is lacking the code to return the found plane's normal
   */
  // Test cross products of pairs of edges
  dReal min1,max1,min2,max2;
  dVector4 plane;
  dVector3 e1,e2,t;
  for(std::set<edge>::iterator i = cvx1.edges.begin();
      i!= cvx1.edges.end();
      ++i)
    {
      // we only need to apply rotation here
      dMULTIPLY0_331 (t,cvx1.final_posr->R,cvx1.points+(i->first*3));
      dMULTIPLY0_331 (e1,cvx1.final_posr->R,cvx1.points+(i->second*3));
      e1[0]-=t[0];
      e1[1]-=t[1];
      e1[2]-=t[2];
      for(std::set<edge>::iterator j = cvx2.edges.begin();
	  j!= cvx2.edges.end();
	  ++j)
	{
	  // we only need to apply rotation here
	  dMULTIPLY0_331 (t,cvx2.final_posr->R,cvx2.points+(j->first*3));
	  dMULTIPLY0_331 (e2,cvx2.final_posr->R,cvx2.points+(j->second*3));
	  e2[0]-=t[0];
	  e2[1]-=t[1];
	  e2[2]-=t[2];
	  dCROSS(plane,=,e1,e2);
	  plane[3]=0;
	  ComputeInterval(cvx1,plane,min1,max1);
	  ComputeInterval(cvx2,plane,min2,max2);
	  if(max2 < min1 || max1 < min2) return false;
	}      
    }
  return true;
}
/*! \brief Does an axis separation test between the 2 convex shapes
using faces and edges */
int TestConvexIntersection(dxConvex& cvx1,dxConvex& cvx2, int flags,
			   dContactGeom *contact, int skip)
{
  int side_index = -1;
  size_t convex_index = 0;
  dReal min_depth=dInfinity;
  int maxc = flags & NUMC_MASK;
  dIASSERT(maxc != 0);
  dxConvex *g1=NULL,*g2=NULL;
  if(!CheckSATConvexFaces(cvx1,cvx2,min_depth,side_index,&g1,&g2))
    {
      return 0;
    }
  else if(!CheckSATConvexFaces(cvx2,cvx1,min_depth,side_index,&g1,&g2))
    {
      return 0;
    }
  else if(!CheckSATConvexEdges(cvx1,cvx2,min_depth,side_index,&g1,&g2))
    {
      return 0;
    }
  // If we get here, there was a collision
  int contacts=0;
  if(g1!=NULL)
    {
      // All points in face are potential contact joints
      unsigned int* pPoly = g1->polygons;
      for(int i=0;i<side_index;++i)
	{
	  pPoly+=pPoly[0]+1;
	}
      for(unsigned int i=0;i<pPoly[0];++i)
	{
	  dMULTIPLY0_331(SAFECONTACT(flags, contact, contacts, skip)->pos,g1->final_posr->R,&g1->points[(pPoly[i+1]*3)]);
	  dVector3Add(g1->final_posr->pos,SAFECONTACT(flags, contact, contacts, skip)->pos,SAFECONTACT(flags, contact, contacts, skip)->pos);
	  ++contacts;	  
	  if (contacts==maxc) return contacts;
	}
    }
  // NOTE: normals are not being set yet
  return contacts;
}

int dCollideConvexConvex (dxGeom *o1, dxGeom *o2, int flags,
			  dContactGeom *contact, int skip)
{
  dIASSERT (skip >= (int)sizeof(dContactGeom));
  dIASSERT (o1->type == dConvexClass);
  dIASSERT (o2->type == dConvexClass);
  dIASSERT ((flags & NUMC_MASK) >= 1);
  dxConvex *Convex1 = (dxConvex*) o1;
  dxConvex *Convex2 = (dxConvex*) o2;
  int contacts = TestConvexIntersection(*Convex1,*Convex2,flags,
				     contact,skip);
  return contacts;
}

#if 0
int dCollideRayConvex (dxGeom *o1, dxGeom *o2, int flags, 
		       dContactGeom *contact, int skip)
{
  dIASSERT (skip >= (int)sizeof(dContactGeom));
  dIASSERT( o1->type == dRayClass );
  dIASSERT( o2->type == dConvexClass );
  dIASSERT ((flags & NUMC_MASK) >= 1);
  dxRay* ray = (dxRay*) o1;
  dxConvex* convex = (dxConvex*) o2;
  dVector3 origin,destination,contactpoint,out;
  dReal depth;
  dVector4 plane;
  unsigned int *pPoly=convex->polygons;
  // Calculate ray origin and destination
  destination[0]=0;
  destination[1]=0;
  destination[2]= ray->length;
  // -- Rotate --
  dMULTIPLY0_331(destination,ray->final_posr->R,destination);
  origin[0]=ray->final_posr->pos[0];
  origin[1]=ray->final_posr->pos[1];
  origin[2]=ray->final_posr->pos[2];
  destination[0]+=origin[0];
  destination[1]+=origin[1];
  destination[2]+=origin[2];
  for(int i=0;i<convex->planecount;++i)
    {
      // Rotate
      dMULTIPLY0_331(plane,convex->final_posr->R,convex->planes+(i*4));
      // Translate
      plane[3]=
	(convex->planes[(i*4)+3])+
	((plane[0] * convex->final_posr->pos[0]) + 
	 (plane[1] * convex->final_posr->pos[1]) + 
	 (plane[2] * convex->final_posr->pos[2]));
      if(IntersectSegmentPlane(origin, 
			       destination, 
			       plane, 
			       depth, 
			       contactpoint))
	{
	  if(IsPointInPolygon(contactpoint,pPoly,convex,out))
	    {
	      contact->pos[0]=contactpoint[0];
	      contact->pos[1]=contactpoint[1];
	      contact->pos[2]=contactpoint[2];
	      contact->normal[0]=plane[0];
	      contact->normal[1]=plane[1];
	      contact->normal[2]=plane[2];
	      contact->depth=depth;
	      contact->g1 = ray;
	      contact->g2 = convex;
	      return 1;
	    }
	}
      pPoly+=pPoly[0]+1;
    }
  return 0;
}
#else
// Ray - Convex collider by David Walters, June 2006
int dCollideRayConvex( dxGeom *o1, dxGeom *o2,
					   int flags, dContactGeom *contact, int skip )
{
	dIASSERT( skip >= (int)sizeof(dContactGeom) );
	dIASSERT( o1->type == dRayClass );
	dIASSERT( o2->type == dConvexClass );
	dIASSERT ((flags & NUMC_MASK) >= 1);

	dxRay* ray = (dxRay*) o1;
	dxConvex* convex = (dxConvex*) o2;

	contact->g1 = ray;
	contact->g2 = convex;

	dReal alpha, beta, nsign;
	int flag;

	//
	// Compute some useful info
	//

	flag = 0;	// Assume start point is behind all planes.

	for ( unsigned int i = 0; i < convex->planecount; ++i )
	{
		// Alias this plane.
		dReal* plane = convex->planes + ( i * 4 );

		// If alpha >= 0 then start point is outside of plane.
		alpha = dDOT( plane, ray->final_posr->pos ) - plane[3];

		// If any alpha is positive, then
		// the ray start is _outside_ of the hull
		if ( alpha >= 0 )
		{
			flag = 1;
			break;
		}
	}

	// If the ray starts inside the convex hull, then everything is flipped.
	nsign = ( flag ) ? REAL( 1.0 ) : REAL( -1.0 );


	//
	// Find closest contact point
	//

	// Assume no contacts.
	contact->depth = dInfinity;

	for ( unsigned int i = 0; i < convex->planecount; ++i )
	{
		// Alias this plane.
		dReal* plane = convex->planes + ( i * 4 );

		// If alpha >= 0 then point is outside of plane.
		alpha = nsign * ( dDOT( plane, ray->final_posr->pos ) - plane[3] );

		// Compute [ plane-normal DOT ray-normal ], (/flip)
		beta = dDOT13( plane, ray->final_posr->R+2 ) * nsign;

		// Ray is pointing at the plane? ( beta < 0 )
		// Ray start to plane is within maximum ray length?
		// Ray start to plane is closer than the current best distance?
		if ( beta < -dEpsilon &&
			 alpha >= 0 && alpha <= ray->length &&
			 alpha < contact->depth )
		{
			// Compute contact point on convex hull surface.
			contact->pos[0] = ray->final_posr->pos[0] + alpha * ray->final_posr->R[0*4+2];
			contact->pos[1] = ray->final_posr->pos[1] + alpha * ray->final_posr->R[1*4+2];
			contact->pos[2] = ray->final_posr->pos[2] + alpha * ray->final_posr->R[2*4+2];

			flag = 0;

			// For all _other_ planes.
			for ( unsigned int j = 0; j < convex->planecount; ++j )
			{
				if ( i == j )
					continue;	// Skip self.

				// Alias this plane.
				dReal* planej = convex->planes + ( j * 4 );

				// If beta >= 0 then start is outside of plane.
				beta = dDOT( planej, contact->pos ) - plane[3];

				// If any beta is positive, then the contact point
				// is not on the surface of the convex hull - it's just
				// intersecting some part of its infinite extent.
				if ( beta > dEpsilon )
				{
					flag = 1;
					break;
				}
			}

			// Contact point isn't outside hull's surface? then it's a good contact!
			if ( flag == 0 )
			{
				// Store the contact normal, possibly flipped.
				contact->normal[0] = nsign * plane[0];
				contact->normal[1] = nsign * plane[1];
				contact->normal[2] = nsign * plane[2];

				// Store depth
				contact->depth = alpha;
				
				if ((flags & CONTACTS_UNIMPORTANT) && contact->depth <= ray->length )
				{
					// Break on any contact if contacts are not important
					break; 
				}
			}
		}
	}
	// Contact?
	return ( contact->depth <= ray->length );
}

#endif
//<-- Convex Collision
