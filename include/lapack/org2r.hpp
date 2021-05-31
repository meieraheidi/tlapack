// Copyright (c) 2013-2021, University of Colorado Denver. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// This program is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.
//
// Created by
/// @author Weslley S Pereira, University of Colorado Denver, USA
//
// Adapted from https://github.com/langou/latl/blob/master/include/org2r.h
/// @author Philippe Theveny, University of Colorado Denver, USA

#ifndef __ORG2R_HH__
#define __ORG2R_HH__

#include "blas/utils.hpp"
#include "lapack/types.hpp"
#include "lapack/larf.hpp"

namespace lapack {

/** Generates a m-by-n matrix Q with orthogonal columns.
 * 
 * @param work Vector of size n-1.
 *     It is possible to use the subarray tau[1:n-1] as the work vector, i.e.,
 *         org2r( ..., tau, &(tau[1]) )
 *     and, in this case, the original vector tau is lost. 
 * @see org2r( blas::size_t, blas::size_t, blas::size_t, TA*, blas::size_t, const Ttau* )
 * 
 * @ingroup geqrf
 */
template<typename TA>
void org2r(
    blas::size_t m, blas::size_t n, blas::size_t k,
    TA* A, blas::size_t lda,
    const TA* tau,
    TA* work )
{
    #define A(i_, j_) A[ (i_) + (j_)*lda ]

    // constants
    const TA zero( 0.0 );
    const TA one( 1.0 );

    // check arguments
    blas_error_if( m < 0 );
    blas_error_if( n < 0 || n > m );
    blas_error_if( k < 0 || k > n );
    blas_error_if( lda < m );

    // quick return
    if (n <= 0) return;
    
    // Initialise columns k:n-1 to columns of the unit matrix
    for (size_t j = k; j < n; ++j) {
        for (size_t l = 0; l < m; ++l)
	        A(l,j) = zero;
        A(j,j) = one;
    }

    for (size_t i = k-1; i > size_t(-1); --i) {

        // Apply $H_{i+1}$ to $A( i:m-1, i:n-1 )$ from the left
        if ( i+1 < n ){
            A(i,i) = one;
            larf( Side::Left, m-i, n-i-1, &(A(i,i)), 1, tau[i], A(i,i+1), lda, work+i );
        }
        if ( i+1 < m )
            scal( m-i-1, -tau[i], &(A(i+1,i)), 1 );
        A(i,i) = one - tau[i];

        // Set A( 0:i-1, i ) to zero
        for (size_t l = 0; l < i; l++)
            A(l,i) = zero;
    }

    #undef A
}

/** Generates a m-by-n matrix Q with orthogonal columns.
 * 
 * @param work Vector of size n-1.
 * @see org2r( blas::size_t, blas::size_t, blas::size_t, TA*, blas::size_t, const Ttau* )
 * 
 * @ingroup geqrf
 */
template<typename TA>
void org2r(
    blas::size_t m, blas::size_t n, blas::size_t k,
    TA* A, blas::size_t lda,
    const real_type<TA>* tau,
    TA* work )
{
    #define A(i_, j_) A[ (i_) + (j_)*lda ]

    // constants
    const TA zero( 0.0 );
    const TA one( 1.0 );

    // check arguments
    blas_error_if( m < 0 );
    blas_error_if( n < 0 || n > m );
    blas_error_if( k < 0 || k > n );
    blas_error_if( lda < m );

    // quick return
    if (n <= 0) return;
    
    // Initialise columns k:n-1 to columns of the unit matrix
    for (size_t j = k; j < n; ++j) {
        for (size_t l = 0; l < m; ++l)
	        A(l,j) = zero;
        A(j,j) = one;
    }

    for (size_t i = k-1; i > size_t(-1); --i) {

        // Apply $H_{i+1}$ to $A( i:m-1, i:n-1 )$ from the left
        if ( i+1 < n ){
            A(i,i) = one;
            larf( Side::Left, m-i, n-i-1, &(A(i,i)), 1, tau[i], A(i,i+1), lda, work );
        }
        if ( i+1 < m )
            scal( m-i-1, -tau[i], &(A(i+1,i)), 1 );
        A(i,i) = one - tau[i];

        // Set A( 0:i-1, i ) to zero
        for (size_t l = 0; l < i; l++)
            A(l,i) = zero;
    }

    #undef A
}

/** Generates a m-by-n matrix Q with orthogonal columns.
 * \[
 *     Q  =  H_1 H_2 ... H_k
 * \]
 * 
 * @param[in] m The number of rows of the matrix A. m>=0
 * @param[in] n The number of columns of the matrix A. n>=0
 * @param[in] k The number of elementary reflectors whose product defines the matrix Q. n>=k>=0
 * @param[in,out] A m-by-n matrix.
 *      On entry, the i-th column must contains the vector which defines the
 *      elementary reflector $H_i$, for $i=0,1,...,k-1$, as returned by GEQRF in the
 *      first k columns of its array argument A.
 *      On exit, the m-by-n matrix $Q  =  H_1 H_2 ... H_k$.
 * @param[in] lda The leading dimension of A. lda >= max(1,m).
 * @param[in] tau Real vector of length min(m,n).      
 *      The scalar factors of the elementary reflectors.
 * 
 * @ingroup geqrf
 */
template< typename TA, typename Ttau >
inline void org2r(
    blas::size_t m, blas::size_t n, blas::size_t k,
    TA* A, blas::size_t lda,
    const Ttau* tau )
{
    TA* work = new TA[ (n > 0) ? n-1 : 0 ];
    org2r( m, n, k, A, lda, tau, work );
    delete[] work;
}

}

#endif // __ORG2R_HH__