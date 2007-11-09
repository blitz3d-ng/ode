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
 *  Sweep and Prune adaptation/tweaks for ODE by Aras Pranckevicius.
 *  Additional work by David Walters
 *  Original code:
 *		OPCODE - Optimized Collision Detection
 *		Copyright (C) 2001 Pierre Terdiman
 *		Homepage: http://www.codercorner.com/Opcode.htm
 *
 *	This version does complete radix sort, not "classical" SAP. So, we
 *	have no temporal coherence, but are able to handle any movement
 *	velocities equally well.
 */

#include <ode/common.h>
#include <ode/matrix.h>
#include <ode/collision_space.h>
#include <ode/collision.h>

#include "collision_kernel.h"
#include "collision_space_internal.h"

// OPCODE's Radix Sorting, returns a list of indices in sorted order
static const uint32* RadixSort( const float* input2, uint32 nb );
// Reference counting helper for radix sort global data.
static void RadixSortRef();
static void RadixSortDeref();


// --------------------------------------------------------------------------
//  SAP space code
// --------------------------------------------------------------------------

struct dxSAPSpace : public dxSpace
{
	// Constructor / Destructor
	dxSAPSpace( dSpaceID _space, int sortaxis );
	~dxSAPSpace();
	
	// dxSpace
	virtual dxGeom* getGeom(int i);
	virtual void add(dxGeom* g);
	virtual void remove(dxGeom* g);
	virtual void dirty(dxGeom* g);
	virtual void computeAABB();
	virtual void cleanGeoms();
	virtual void collide( void *data, dNearCallback *callback );
	virtual void collide2( void *data, dxGeom *geom, dNearCallback *callback );

private:

	//--------------------------------------------------------------------------
	// Local Declarations
	//--------------------------------------------------------------------------

	//! A generic couple structure
	struct Pair
	{
		uint32 id0;	//!< First index of the pair
		uint32 id1;	//!< Second index of the pair

		// Default and Value Constructor
		Pair() {}
		Pair( uint32 i0, uint32 i1 ) : id0( i0 ), id1( i1 ) {}
	};

	typedef dArray<dxGeom*> TGeomPtrArray;

	//--------------------------------------------------------------------------
	// Helpers
	//--------------------------------------------------------------------------

	/**
	 *	Complete box pruning.
	 *  Returns a list of overlapping pairs of boxes, each box of the pair
	 *  belongs to the same set.
	 *
	 *	@param	count	[in] number of boxes.
	 *	@param	geoms	[in] geoms of boxes.
	 *	@param	pairs	[out] array of overlapping pairs.
	 */
	void BoxPruning( int count, const dxGeom** geoms, dArray< Pair >& pairs );

	// The pruning scratch pad is always growing as geoms are added.
	void GrowScratchPad( int count );

	// Reset pruning scratch pad
	void ResetScratchPad();


	//--------------------------------------------------------------------------
	// Implementation Data
	//--------------------------------------------------------------------------

	// We have two lists (arrays of pointers) to dirty and clean
	// geoms. Each geom knows it's index into the corresponding list
	// (see macros above).
	TGeomPtrArray DirtyList; // dirty geoms
	TGeomPtrArray GeomList;	// clean geoms

	// For SAP, we ultimately separate "normal" geoms and the ones that have
	// infinite AABBs. No point doing SAP on infinite ones (and it doesn't handle
	// infinite geoms anyway).
	TGeomPtrArray	TmpGeomList;	// temporary for normal geoms
	TGeomPtrArray	TmpInfGeomList;	// temporary for geoms with infinite AABBs

	// Our sorting axes. (X,Z,Y is often best). Store *2 for minor speedup
	// Axis indices into geom's aabb are: min=idx, max=idx+1
	uint32 ax0idx;
	uint32 ax1idx;
	uint32 ax2idx;

	// pruning position array scratch pad
	// NOTE: this is floats because of the OPCODE radix sorter
	float* poslist;
	int scratch_size;
};

// Creation
dSpaceID dSweepAndPruneSpaceCreate( dxSpace* space, int axisorder ) {
	return new dxSAPSpace( space, axisorder );
}


//==============================================================================

#define GEOM_ENABLED(g) ((g)->gflags & GEOM_ENABLED)

// HACK: We abuse 'next' and 'tome' members of dxGeom to store indices into dirty/geom lists.
#define GEOM_SET_DIRTY_IDX(g,idx) { g->next = (dxGeom*)(idx); }
#define GEOM_SET_GEOM_IDX(g,idx) { g->tome = (dxGeom**)(idx); }
#define GEOM_GET_DIRTY_IDX(g) ((int)g->next)
#define GEOM_GET_GEOM_IDX(g) ((int)g->tome)
#define GEOM_INVALID_IDX (-1)


/*
 *  A bit of repetitive work - similar to collideAABBs, but doesn't check
 *  if AABBs intersect (because SAP returns pairs with overlapping AABBs).
 */
static void collideGeomsNoAABBs( dxGeom *g1, dxGeom *g2, void *data, dNearCallback *callback )
{
	dIASSERT( (g1->gflags & GEOM_AABB_BAD)==0 );
	dIASSERT( (g2->gflags & GEOM_AABB_BAD)==0 );
	
	// no contacts if both geoms on the same body, and the body is not 0
	if (g1->body == g2->body && g1->body) return;
	
	// test if the category and collide bitfields match
	if ( ((g1->category_bits & g2->collide_bits) ||
		  (g2->category_bits & g1->collide_bits)) == 0) {
		return;
	}
	
	dReal *bounds1 = g1->aabb;
	dReal *bounds2 = g2->aabb;
	
	// check if either object is able to prove that it doesn't intersect the
	// AABB of the other
	if (g1->AABBTest (g2,bounds2) == 0) return;
	if (g2->AABBTest (g1,bounds1) == 0) return;
	
	// the objects might actually intersect - call the space callback function
	callback (data,g1,g2);
};


dxSAPSpace::dxSAPSpace( dSpaceID _space, int axisorder ) : dxSpace( _space ), poslist( NULL ), scratch_size( 0 )
{
	type = dSweepAndPruneSpaceClass;

	// Init AABB to infinity
	aabb[0] = -dInfinity;
	aabb[1] = dInfinity;
	aabb[2] = -dInfinity;
	aabb[3] = dInfinity;
	aabb[4] = -dInfinity;
	aabb[5] = dInfinity;

	ax0idx = ( ( axisorder ) & 3 ) << 1;
	ax1idx = ( ( axisorder >> 2 ) & 3 ) << 1;
	ax2idx = ( ( axisorder >> 4 ) & 3 ) << 1;

	// We want the Radix sort to stick around. 
	RadixSortRef();
}

dxSAPSpace::~dxSAPSpace()
{
	CHECK_NOT_LOCKED(this);
	if ( cleanup ) {
		// note that destroying each geom will call remove()
		for ( ; DirtyList.size(); dGeomDestroy( DirtyList[ 0 ] ) ) {}
		for ( ; GeomList.size(); dGeomDestroy( GeomList[ 0 ] ) ) {}
	}
	else {
		// just unhook them
		for ( ; DirtyList.size(); remove( DirtyList[ 0 ] ) ) {}
		for ( ; GeomList.size(); remove( GeomList[ 0 ] ) ) {}
	}
	
	// Free scratch pad
	ResetScratchPad();

	// We're done with the Radix sorter
	RadixSortDeref();
}

void dxSAPSpace::GrowScratchPad( int count )
{	
	if ( count > scratch_size )
	{
		if ( poslist )
			delete[] poslist;

		// Allocate the temporary data +1
		poslist = new float[ count + 1 ];
		scratch_size = count;
	}
}

void dxSAPSpace::ResetScratchPad()
{	
	if ( poslist )
		delete[] poslist;
	scratch_size = 0;
}

dxGeom* dxSAPSpace::getGeom( int i )
{
	dUASSERT( i >= 0 && i < count, "index out of range" );
	int dirtySize = DirtyList.size();
	if( i < dirtySize )
		return DirtyList[i];
	else
		return GeomList[i-dirtySize];
}

void dxSAPSpace::add( dxGeom* g )
{
	CHECK_NOT_LOCKED (this);
	dAASSERT(g);
	dUASSERT(g->parent_space == 0 && g->next == 0, "geom is already in a space");

	g->gflags |= GEOM_DIRTY | GEOM_AABB_BAD;
	
	// add to dirty list
	GEOM_SET_DIRTY_IDX( g, DirtyList.size() );
	GEOM_SET_GEOM_IDX( g, GEOM_INVALID_IDX );
	DirtyList.push( g );

	g->parent_space = this;
	this->count++;

	dGeomMoved(this);
}

void dxSAPSpace::remove( dxGeom* g )
{
	CHECK_NOT_LOCKED(this);
	dAASSERT(g);
	dUASSERT(g->parent_space == this,"object is not in this space");
	
	// remove
	int dirtyIdx = GEOM_GET_DIRTY_IDX(g);
	int geomIdx = GEOM_GET_GEOM_IDX(g);
	// must be in one list, not in both
	dUASSERT(
		dirtyIdx==GEOM_INVALID_IDX && geomIdx>=0 && geomIdx<GeomList.size() ||
		geomIdx==GEOM_INVALID_IDX && dirtyIdx>=0 && dirtyIdx<DirtyList.size(),
		"geom indices messed up" );
	if( dirtyIdx != GEOM_INVALID_IDX ) {
		// we're in dirty list, remove
		int dirtySize = DirtyList.size();
		dxGeom* lastG = DirtyList[dirtySize-1];
		DirtyList[dirtyIdx] = lastG;
		GEOM_SET_DIRTY_IDX(lastG,dirtyIdx);
		GEOM_SET_DIRTY_IDX(g,GEOM_INVALID_IDX);
		DirtyList.setSize( dirtySize-1 );
	} else {
		// we're in geom list, remove
		int geomSize = GeomList.size();
		dxGeom* lastG = GeomList[geomSize-1];
		GeomList[geomIdx] = lastG;
		GEOM_SET_GEOM_IDX(lastG,geomIdx);
		GEOM_SET_GEOM_IDX(g,GEOM_INVALID_IDX);
		GeomList.setSize( geomSize-1 );
	}
	count--;

	// safeguard
	g->parent_space = 0;
	
	// the bounding box of this space (and that of all the parents) may have
	// changed as a consequence of the removal.
	dGeomMoved(this);
}

void dxSAPSpace::dirty( dxGeom* g )
{
	dAASSERT(g);
	dUASSERT(g->parent_space == this,"object is not in this space");
	
	// check if already dirtied
	int dirtyIdx = GEOM_GET_DIRTY_IDX(g);
	if( dirtyIdx != GEOM_INVALID_IDX )
		return;

	int geomIdx = GEOM_GET_GEOM_IDX(g);
	dUASSERT( geomIdx>=0 && geomIdx<GeomList.size(), "geom indices messed up" );

	// remove from geom list, place last in place of this
	int geomSize = GeomList.size();
	dxGeom* lastG = GeomList[geomSize-1];
	GeomList[geomIdx] = lastG;
	GEOM_SET_GEOM_IDX(lastG,geomIdx);
	GeomList.setSize( geomSize-1 );

	// add to dirty list
	GEOM_SET_GEOM_IDX( g, GEOM_INVALID_IDX );
	GEOM_SET_DIRTY_IDX( g, DirtyList.size() );
	DirtyList.push( g );
}

void dxSAPSpace::computeAABB()
{
	// TODO?
}

void dxSAPSpace::cleanGeoms()
{
	int dirtySize = DirtyList.size();
	if( !dirtySize )
		return;

	// compute the AABBs of all dirty geoms, clear the dirty flags,
	// remove from dirty list, place into geom list
	lock_count++;
	
	int geomSize = GeomList.size();
	GeomList.setSize( geomSize + dirtySize ); // ensure space in geom list

	for( int i = 0; i < dirtySize; ++i ) {
		dxGeom* g = DirtyList[i];
		if( IS_SPACE(g) ) {
			((dxSpace*)g)->cleanGeoms();
		}
		g->recomputeAABB();
		g->gflags &= (~(GEOM_DIRTY|GEOM_AABB_BAD));
		// remove from dirty list, add to geom list
		GEOM_SET_DIRTY_IDX( g, GEOM_INVALID_IDX );
		GEOM_SET_GEOM_IDX( g, geomSize + i );
		GeomList[geomSize+i] = g;
	}
	// clear dirty list
	DirtyList.setSize( 0 );

	lock_count--;
}

void dxSAPSpace::collide( void *data, dNearCallback *callback )
{
	dAASSERT (callback);

	lock_count++;
	
	cleanGeoms();
	
	// by now all geoms are in GeomList, and DirtyList must be empty
	int geom_count = GeomList.size();
	dUASSERT( geom_count == count, "geom counts messed up" );

	// separate all ENABLED geoms into infinite AABBs and normal AABBs
	TmpGeomList.setSize(0);
	TmpInfGeomList.setSize(0);
	int axis0max = ax0idx + 1;
	for( int i = 0; i < geom_count; ++i ) {
		dxGeom* g = GeomList[i];
		if( !GEOM_ENABLED(g) ) // skip disabled ones
			continue;
		const dReal& amax = g->aabb[axis0max];
		if( amax == dInfinity ) // HACK? probably not...
			TmpInfGeomList.push( g );
		else
			TmpGeomList.push( g );
	}

	// do SAP on normal AABBs
	dArray< Pair > overlapBoxes;
	int tmp_geom_count = TmpGeomList.size();
	if ( tmp_geom_count > 0 )
	{
		GrowScratchPad( tmp_geom_count );
		BoxPruning( tmp_geom_count, (const dxGeom**)TmpGeomList.data(), overlapBoxes );
	}

	// collide overlapping
	int overlapCount = overlapBoxes.size();
	for( int j = 0; j < overlapCount; ++j )
	{
		const Pair& pair = overlapBoxes[ j ];
		dxGeom* g1 = TmpGeomList[ pair.id0 ];
		dxGeom* g2 = TmpGeomList[ pair.id1 ];
		collideGeomsNoAABBs( g1, g2, data, callback );
	}

	int infSize = TmpInfGeomList.size();
	int normSize = TmpGeomList.size();
	int m, n;

	for ( m = 0; m < infSize; ++m )
	{
		dxGeom* g1 = TmpInfGeomList[ m ];
		
		// collide infinite ones
		for( n = m+1; n < infSize; ++n ) {
			dxGeom* g2 = TmpInfGeomList[n];
			collideGeomsNoAABBs( g1, g2, data, callback );
		}
		
		// collide infinite ones with normal ones
		for( n = 0; n < normSize; ++n ) {
			dxGeom* g2 = TmpGeomList[n];
			collideGeomsNoAABBs( g1, g2, data, callback );
		}
	}
	
	lock_count--;
}

void dxSAPSpace::collide2( void *data, dxGeom *geom, dNearCallback *callback )
{
	dAASSERT (geom && callback);

	// TODO: This is just a simple N^2 implementation

	lock_count++;

	cleanGeoms();
	geom->recomputeAABB();

	// intersect bounding boxes
	int geom_count = GeomList.size();
	for ( int i = 0; i < geom_count; ++i ) {
		dxGeom* g = GeomList[i];
		if ( GEOM_ENABLED(g) )
			collideAABBs (g,geom,data,callback);
	}

	lock_count--;
}


void dxSAPSpace::BoxPruning( int count, const dxGeom** geoms, dArray< Pair >& pairs )
{
	// 1) Build main list using the primary axis
	//  NOTE: uses floats instead of dReals because that's what radix sort wants
	for( int i = 0; i < count; ++i )
		poslist[ i ] = (float)TmpGeomList[i]->aabb[ ax0idx ];
	poslist[ count++ ] = ODE_INFINITY4;

	// 2) Sort the list
	const uint32* Sorted = RadixSort( poslist, count );

	// 3) Prune the list
	const uint32* const LastSorted = Sorted + count;
	const uint32* RunningAddress = Sorted;
	
	Pair IndexPair;
	while ( RunningAddress < LastSorted && Sorted < LastSorted )
	{
		IndexPair.id0 = *Sorted++;

		// empty, this loop just advances RunningAddress
		while ( poslist[*RunningAddress++] < poslist[IndexPair.id0] ) {}

		if ( RunningAddress < LastSorted )
		{
			const uint32* RunningAddress2 = RunningAddress;

			const dReal idx0ax0max = geoms[IndexPair.id0]->aabb[ax0idx+1];
			const dReal idx0ax1max = geoms[IndexPair.id0]->aabb[ax1idx+1];
			const dReal idx0ax2max = geoms[IndexPair.id0]->aabb[ax2idx+1];
			
			while ( poslist[ IndexPair.id1 = *RunningAddress2++ ] <= idx0ax0max )
			{
				const dReal* aabb0 = geoms[ IndexPair.id0 ]->aabb;
				const dReal* aabb1 = geoms[ IndexPair.id1 ]->aabb;

				// Intersection?
				if ( idx0ax1max >= aabb1[ax1idx] && aabb1[ax1idx+1] >= aabb0[ax1idx] )
				if ( idx0ax2max >= aabb1[ax2idx] && aabb1[ax2idx+1] >= aabb0[ax2idx] )
				{
					pairs.push( IndexPair );
				}
			}
		}

	}; // while ( RunningAddress < LastSorted && Sorted < LastSorted )
}


//==============================================================================

//------------------------------------------------------------------------------
// Radix Sort
//------------------------------------------------------------------------------

#define INVALIDATE_RANKS	mCurrentSize|=0x80000000
#define VALIDATE_RANKS		mCurrentSize&=0x7fffffff
#define CURRENT_SIZE		(mCurrentSize&0x7fffffff)
#define INVALID_RANKS		(mCurrentSize&0x80000000)

static int radixsort_ref = 0;					// Reference counter
static uint32 mCurrentSize;						//!< Current size of the indices list
static uint32* mRanks1;							//!< Two lists, swapped each pass
static uint32* mRanks2;

static void RadixSortRef()
{
	if ( radixsort_ref == 0 )
	{
		mRanks1 = NULL;
		mRanks2 = NULL;
		INVALIDATE_RANKS;
	}

	++radixsort_ref;
}

static void RadixSortDeref()
{
	--radixsort_ref;

	if ( radixsort_ref == 0 )
	{
		// Release everything
		if ( mRanks1 ) { delete[] mRanks1; mRanks1 = NULL; }
		if ( mRanks2 ) { delete[] mRanks2; mRanks2 = NULL; }
		
		// Allow us to restart
		mCurrentSize = 0;
		INVALIDATE_RANKS;
	}
}

#define CHECK_PASS_VALIDITY(pass)															\
	/* Shortcut to current counters */														\
	uint32* CurCount = &mHistogram[pass<<8];												\
																							\
	/* Reset flag. The sorting pass is supposed to be performed. (default) */				\
	bool PerformPass = true;																\
																							\
	/* Check pass validity */																\
																							\
	/* If all values have the same byte, sorting is useless. */								\
	/* It may happen when sorting bytes or words instead of dwords. */						\
	/* This routine actually sorts words faster than dwords, and bytes */					\
	/* faster than words. Standard running time (O(4*n))is reduced to O(2*n) */				\
	/* for words and O(n) for bytes. Running time for floats depends on actual values... */	\
																							\
	/* Get first byte */																	\
	uint8 UniqueVal = *(((uint8*)input)+pass);												\
																							\
	/* Check that byte's counter */															\
	if(CurCount[UniqueVal]==nb)	PerformPass=false;

// WARNING ONLY SORTS IEEE FLOATING-POINT VALUES
static const uint32* RadixSort( const float* input2, uint32 nb )
{
	uint32* input = (uint32*)input2;

	// Resize lists if needed
	uint32 CurSize = CURRENT_SIZE;
	if ( nb != CurSize )
	{
		// Grow?
		if ( nb > CurSize )
		{
			// Free previously used ram
			delete[] mRanks2;
			delete[] mRanks1;

			// Get some fresh one
			mRanks1 = new uint32[nb];
			mRanks2	= new uint32[nb];
		}

		mCurrentSize = nb;
		INVALIDATE_RANKS;
	}

	// Allocate histograms & offsets on the stack
	uint32 mHistogram[256*4];
	uint32* mLink[256];

	// Create histograms (counters). Counters for all passes are created in one run.
	// Pros:	read input buffer once instead of four times
	// Cons:	mHistogram is 4Kb instead of 1Kb
	// Floating-point values are always supposed to be signed values, so there's only one code path there.
	// Please note the floating point comparison needed for temporal coherence! Although the resulting asm code
	// is dreadful, this is surprisingly not such a performance hit - well, I suppose that's a big one on first
	// generation Pentiums....We can't make comparison on integer representations because, as Chris said, it just
	// wouldn't work with mixed positive/negative values....
	{
		/* Clear counters/histograms */															
		memset(mHistogram, 0, 256*4*sizeof(uint32));											
																								
		/* Prepare to count */																	
		uint8* p = (uint8*)input;																
		uint8* pe = &p[nb*4];																	
		uint32* h0= &mHistogram[0];		/* Histogram for first pass (LSB)	*/					
		uint32* h1= &mHistogram[256];	/* Histogram for second pass		*/					
		uint32* h2= &mHistogram[512];	/* Histogram for third pass			*/					
		uint32* h3= &mHistogram[768];	/* Histogram for last pass (MSB)	*/					
																								
		bool AlreadySorted = true;	/* Optimism... */											
																								
		if(INVALID_RANKS)																		
		{																						
			/* Prepare for temporal coherence */												
			float* Running = (float*)input2;													
			float PrevVal = *Running;															
																								
			while(p!=pe)																		
			{																					
				/* Read input input2 in previous sorted order */								
				float Val = *Running++;															
				/* Check whether already sorted or not */										
				if(Val<PrevVal)	{ AlreadySorted = false; break; } /* Early out */				
				/* Update for next iteration */													
				PrevVal = Val;																	
																								
				/* Create histograms */															
				h0[*p++]++;	h1[*p++]++;	h2[*p++]++;	h3[*p++]++;									
			}																					
																								
			/* If all input values are already sorted, we just have to return and leave the */	
			/* previous list unchanged. That way the routine may take advantage of temporal */	
			/* coherence, for example when used to sort transparent faces.					*/	
			if(AlreadySorted)																	
			{																					
				for(uint32 i=0;i<nb;i++)	mRanks1[i] = i;										
				return mRanks1;																	
			}																					
		}																						
		else																					
		{																						
			/* Prepare for temporal coherence */												
			uint32* Indices = mRanks1;															
			float PrevVal = (float)input2[*Indices];											
																								
			while(p!=pe)																		
			{																					
				/* Read input input2 in previous sorted order */								
				float Val = (float)input2[*Indices++];											
				/* Check whether already sorted or not */										
				if(Val<PrevVal)	{ AlreadySorted = false; break; } /* Early out */				
				/* Update for next iteration */													
				PrevVal = Val;																	
																								
				/* Create histograms */															
				h0[*p++]++;	h1[*p++]++;	h2[*p++]++;	h3[*p++]++;									
			}																					
																								
			/* If all input values are already sorted, we just have to return and leave the */	
			/* previous list unchanged. That way the routine may take advantage of temporal */	
			/* coherence, for example when used to sort transparent faces.					*/	
			if(AlreadySorted)	{ return mRanks1;	}											
		}																						
																								
		/* Else there has been an early out and we must finish computing the histograms */		
		while(p!=pe)																			
		{																						
			/* Create histograms without the previous overhead */								
			h0[*p++]++;	h1[*p++]++;	h2[*p++]++;	h3[*p++]++;										
		}
	}

	// Compute #negative values involved if needed
	uint32 NbNegativeValues = 0;
	
	// An efficient way to compute the number of negatives values we'll have to deal with is simply to sum the 128
	// last values of the last histogram. Last histogram because that's the one for the Most Significant Byte,
	// responsible for the sign. 128 last values because the 128 first ones are related to positive numbers.
	uint32* h3= &mHistogram[768];
	for(uint32 i=128;i<256;i++)	NbNegativeValues += h3[i];	// 768 for last histogram, 128 for negative part

	// Radix sort, j is the pass number (0=LSB, 3=MSB)
	for(uint32 j=0;j<4;j++)
	{
		// Should we care about negative values?
		if(j!=3)
		{
			// Here we deal with positive values only
			CHECK_PASS_VALIDITY(j);

			if(PerformPass)
			{
				// Create offsets
				mLink[0] = mRanks2;
				for(uint32 i=1;i<256;i++)		mLink[i] = mLink[i-1] + CurCount[i-1];

				// Perform Radix Sort
				uint8* InputBytes = (uint8*)input;
				InputBytes += j;
				if(INVALID_RANKS)
				{
					for(uint32 i=0;i<nb;i++)	*mLink[InputBytes[i<<2]]++ = i;
					VALIDATE_RANKS;
				}
				else
				{
					uint32* Indices		= mRanks1;
					uint32* IndicesEnd	= &mRanks1[nb];
					while(Indices!=IndicesEnd)
					{
						uint32 id = *Indices++;
						*mLink[InputBytes[id<<2]]++ = id;
					}
				}

				// Swap pointers for next pass. Valid indices - the most recent ones - are in mRanks after the swap.
				uint32* Tmp	= mRanks1;	mRanks1 = mRanks2; mRanks2 = Tmp;
			}
		}
		else
		{
			// This is a special case to correctly handle negative values
			CHECK_PASS_VALIDITY(j);

			if(PerformPass)
			{
				// Create biased offsets, in order for negative numbers to be sorted as well
				mLink[0] = &mRanks2[NbNegativeValues];										// First positive number takes place after the negative ones
				for(uint32 i=1;i<128;i++)		mLink[i] = mLink[i-1] + CurCount[i-1];		// 1 to 128 for positive numbers

				// We must reverse the sorting order for negative numbers!
				mLink[255] = mRanks2;
				for(uint32 i=0;i<127;i++)	mLink[254-i] = mLink[255-i] + CurCount[255-i];		// Fixing the wrong order for negative values
				for(uint32 i=128;i<256;i++)	mLink[i] += CurCount[i];							// Fixing the wrong place for negative values

				// Perform Radix Sort
				if(INVALID_RANKS)
				{
					for(uint32 i=0;i<nb;i++)
					{
						uint32 Radix = input[i]>>24;							// Radix byte, same as above. AND is useless here (uint32).
						// ### cmp to be killed. Not good. Later.
						if(Radix<128)		*mLink[Radix]++ = i;		// Number is positive, same as above
						else				*(--mLink[Radix]) = i;		// Number is negative, flip the sorting order
					}
					VALIDATE_RANKS;
				}
				else
				{
					for(uint32 i=0;i<nb;i++)
					{
						uint32 Radix = input[mRanks1[i]]>>24;							// Radix byte, same as above. AND is useless here (uint32).
						// ### cmp to be killed. Not good. Later.
						if(Radix<128)		*mLink[Radix]++ = mRanks1[i];		// Number is positive, same as above
						else				*(--mLink[Radix]) = mRanks1[i];		// Number is negative, flip the sorting order
					}
				}
				// Swap pointers for next pass. Valid indices - the most recent ones - are in mRanks after the swap.
				uint32* Tmp	= mRanks1;	mRanks1 = mRanks2; mRanks2 = Tmp;
			}
			else
			{
				// The pass is useless, yet we still have to reverse the order of current list if all values are negative.
				if(UniqueVal>=128)
				{
					if(INVALID_RANKS)
					{
						// ###Possible?
						for(uint32 i=0;i<nb;i++)	mRanks2[i] = nb-i-1;
						VALIDATE_RANKS;
					}
					else
					{
						for(uint32 i=0;i<nb;i++)	mRanks2[i] = mRanks1[nb-i-1];
					}

					// Swap pointers for next pass. Valid indices - the most recent ones - are in mRanks after the swap.
					uint32* Tmp	= mRanks1;	mRanks1 = mRanks2; mRanks2 = Tmp;
				}
			}
		}
	}

	// Return indices
	return mRanks1;
}
