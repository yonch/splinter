/*
 * This file is part of the SPLINTER library.
 * Copyright (C) 2012 Bjarne Grimstad (bjarne.grimstad@gmail.com).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "bsplinebuilder.h"
#include "mykroneckerproduct.h"
#include "unsupported/Eigen/KroneckerProduct"
#include <linearsolvers.h>
#include <serializer.h>
#include <iostream>
#include <utilities.h>

namespace SPLINTER
{
// Default constructor
BSpline::Builder::Builder(const DataTable &data)
        :
        _data(data),
        _degrees(getBSplineDegrees(data.getNumVariables(), 3)),
        _numBasisFunctions(std::vector<unsigned int>(data.getNumVariables(), 0)),
        _knotSpacing(KnotSpacing::AS_SAMPLED),
        _smoothing(Smoothing::NONE),
        _alpha(0.1),
        _padding(0.0),
        _hfsIters(0)
{
}

/*
 * Build B-spline
 */
BSpline BSpline::Builder::build() const
{
    // Check data
    // TODO: Remove this test
#ifndef SPLINTER_ALLOW_SCATTER
    if (!_data.isGridComplete())
        throw Exception("BSpline::Builder::build: Cannot create B-spline from irregular (incomplete) grid.");
#endif

    // Build knot vectors
    auto knotVectors = computeKnotVectors();

    // Build B-spline (with default coefficients)
    auto bspline = BSpline(knotVectors, _degrees);

    // Compute coefficients from samples and update B-spline
    auto coefficients = computeCoefficients(bspline);
    bspline.setCoefficients(coefficients);

    return bspline;
}

/*
 * Find coefficients of B-spline by solving:
 * min ||A*x - b||^2 + alpha*||R||^2,
 * where
 * A = mxn matrix of n basis functions evaluated at m sample points,
 * b = vector of m sample points y-values (or x-values when calculating knot averages),
 * x = B-spline coefficients (or knot averages),
 * R = Regularization matrix,
 * alpha = regularization parameter.
 */
DenseVector BSpline::Builder::computeCoefficients(const BSpline& bspline) const
{
    SparseMatrix B = computeBasisFunctionMatrix(bspline);
    SparseMatrix A = B;
    DenseVector b = getSamplePointValues();

    if (_smoothing == Smoothing::IDENTITY)
    {
        /*
         * Computing B-spline coefficients with a regularization term
         * ||Ax-b||^2 + alpha*x^T*x
         *
         * NOTE: This corresponds to a Tikhonov regularization (or ridge regression) with the Identity matrix.
         * See: https://en.wikipedia.org/wiki/Tikhonov_regularization
         *
         * NOTE2: consider changing regularization factor to (alpha/numSample)
         */
        SparseMatrix Bt = B.transpose();
        A = Bt*B;
        b = Bt*b;

        auto I = SparseMatrix(A.cols(), A.cols());
        I.setIdentity();
        A += _alpha*I;
    }
    else if (_smoothing == Smoothing::PSPLINE)
    {
        /*
         * The P-Spline is a smooting B-spline which relaxes the interpolation constraints on the control points to allow
         * smoother spline curves. It minimizes an objective which penalizes both deviation from sample points (to lower bias)
         * and the magnitude of second derivatives (to lower variance).
         *
         * Setup and solve equations Ax = b,
         * A = B'*W*B + l*D'*D
         * b = B'*W*y
         * x = control coefficients or knot averages.
         * B = basis functions at sample x-values,
         * W = weighting matrix for interpolating specific points
         * D = second-order finite difference matrix
         * l = penalizing parameter (increase for more smoothing)
         * y = sample y-values when calculating control coefficients,
         * y = sample x-values when calculating knot averages
         */

        // Assuming regular grid
        unsigned int numSamples = _data.getNumSamples();

        // get \lambda, the smoothing parameter
        auto l = _alpha;

        SparseMatrix Bt = B.transpose();

        // Weight matrix
        auto W = getWeightMatrix();

        // Second order finite difference matrix
        SparseMatrix D = getSecondOrderFiniteDifferenceMatrix(bspline);

        // Left-hand side matrix
        SparseMatrix BtW = Bt*W;
        SparseMatrix BtWB = BtW*B;
        A = BtWB + l*D.transpose()*D;

        // Save y the sampled values
        DenseVector y = b;

        // Compute right-hand side matrices
        b = Bt*W*b;

        // Optimize smoothing parameter alpha using the HFS algorithm
        // For a description of HFS, see Chapter 3.4:
        //   Eilers, Paul H.C.; Marx, Brian D.. Practical Smoothing (The Joys of P-splines). Cambridge University Press.
        for (int hfsIter = 0; hfsIter < _hfsIters; hfsIter++) {
            // invert A = (B'WB + lD'D)
            auto Ainv = Eigen::PartialPivLU<DenseMatrix>(A.toDense()).inverse();
            // compute G = (B'WB + lD'D)^-1 B'WB
            auto G = (Ainv*BtWB);
            // ED = trace(G), the effective model dimension
            double ED = G.trace();
            // Estimate x (book calls this alpha)
            DenseVector x = Ainv * (BtW * y);

#ifdef HFS_USE_BOOK_TAU_SIGMA
            // Method 1: book
            // tau^2 = ||D x||^2 / (ED - d)
            double tau_squared = (D * x).squaredNorm() / (ED - _data.getNumVariables());
            // sigma^2 = ||y - B x||^2 / (m - ED)
            double sigma_squared = (y - (B * x)).squaredNorm() / (_data.getNumSamples() - ED);
#else
            // Method 2: From code example https://psplines.bitbucket.io/Docs/doc-f-HFS-convergence.pdf
            // tau^2 = ||D x||^2 / ED 
            double tau_squared = (D * x).squaredNorm() / ED;
            // sigma^2 = ||y - B x||^2 / (m - d - ED)
            double sigma_squared = (y - (B * x)).squaredNorm() / (_data.getNumSamples()-_data.getNumVariables() - ED);
#endif

            // update \lambda = \sigma^2 / \tau^2
            l = sigma_squared / tau_squared;
            // we'll need to update A with new \lambda for next iteration or solving
            A = BtWB + l*D.transpose()*D;
#ifndef NDEBUG
            std::cout << "HFS iteration " << hfsIter 
                      << " new alpha is " << l 
                      << " ED=" << ED
                      << " tau^2=" << tau_squared 
                      << " sigma^2=" << sigma_squared << "\n";
#endif
        }
#if 0
        std::cout << "P-Spline alpha is " << l << "\n";
#endif
    }

    DenseVector x;

    int numEquations = A.rows();
    int maxNumEquations = 100;
    bool solveAsDense = (numEquations < maxNumEquations);

    if (!solveAsDense)
    {
#ifndef NDEBUG
        std::cout << "BSpline::Builder::computeBSplineCoefficients: Computing B-spline control points using sparse solver." << std::endl;
#endif // NDEBUG

        SparseLU<> s;
        //bool successfulSolve = (s.solve(A,Bx,Cx) && s.solve(A,By,Cy));

        solveAsDense = !s.solve(A, b, x);
    }

    if (solveAsDense)
    {
#ifndef NDEBUG
        std::cout << "BSpline::Builder::computeBSplineCoefficients: Computing B-spline control points using dense solver." << std::endl;
#endif // NDEBUG

        DenseMatrix Ad = A.toDense();
        DenseQR<DenseVector> s;
        // DenseSVD<DenseVector> s;
        //bool successfulSolve = (s.solve(Ad,Bx,Cx) && s.solve(Ad,By,Cy));
        if (!s.solve(Ad, b, x))
        {
            throw Exception("BSpline::Builder::computeBSplineCoefficients: Failed to solve for B-spline coefficients.");
        }
    }

    return x;
}

SparseMatrix BSpline::Builder::computeBasisFunctionMatrix(const BSpline &bspline) const
{
    unsigned int numVariables = _data.getNumVariables();
    unsigned int numSamples = _data.getNumSamples();

    // TODO: Reserve nnz per row (degree+1)
    //int nnzPrCol = bspline.basis.supportedPrInterval();

    SparseMatrix A(numSamples, bspline.getNumBasisFunctions());
    //A.reserve(DenseVector::Constant(numSamples, nnzPrCol)); // TODO: should reserve nnz per row!

    int i = 0;
    for (auto it = _data.cbegin(); it != _data.cend(); ++it, ++i)
    {
        DenseVector xi(numVariables);
        xi.setZero();
        std::vector<double> xv = it->getX();
        for (unsigned int j = 0; j < numVariables; ++j)
        {
            xi(j) = xv.at(j);
        }

        SparseVector basisValues = bspline.evalBasis(xi);

        for (SparseVector::InnerIterator it2(basisValues); it2; ++it2)
        {
            A.insert(i,it2.index()) = it2.value();
        }
    }

    A.makeCompressed();

    return A;
}

DenseVector BSpline::Builder::getSamplePointValues() const
{
    DenseVector B = DenseVector::Zero(_data.getNumSamples());

    int i = 0;
    for (auto it = _data.cbegin(); it != _data.cend(); ++it, ++i)
        B(i) = it->getY();

    return B;
}

/*
* Function for generating second order finite-difference matrix, which is used for penalizing the
* (approximate) second derivative in control point calculation for P-splines.
*/
SparseMatrix BSpline::Builder::getSecondOrderFiniteDifferenceMatrix(const BSpline &bspline) const
{
    unsigned int numVariables = bspline.getNumVariables();

    // Number of (total) basis functions - defines the number of columns in D
    unsigned int numCols = bspline.getNumBasisFunctions();
    std::vector<unsigned int> numBasisFunctions = bspline.getNumBasisFunctionsPerVariable();

    // Number of basis functions (and coefficients) in each variable
    std::vector<unsigned int> dims;
    for (unsigned int i = 0; i < numVariables; i++)
        dims.push_back(numBasisFunctions.at(i));

    std::reverse(dims.begin(), dims.end());

    for (unsigned int i=0; i < numVariables; ++i)
        if (numBasisFunctions.at(i) < 3)
            throw Exception("BSpline::Builder::getSecondOrderDifferenceMatrix: Need at least three coefficients/basis function per variable.");

    // Number of rows in D and in each block
    int numRows = 0;
    std::vector< int > numBlkRows;
    for (unsigned int i = 0; i < numVariables; i++)
    {
        int prod = 1;
        for (unsigned int j = 0; j < numVariables; j++)
        {
            if (i == j)
                prod *= (dims[j] - 2);
            else
                prod *= dims[j];
        }
        numRows += prod;
        numBlkRows.push_back(prod);
    }

    // Resize and initialize D
    SparseMatrix D(numRows, numCols);
    D.reserve(DenseVector::Constant(numCols,2*numVariables));   // D has no more than two elems per col per dim

    int i = 0;                                          // Row index
    // Loop though each dimension (each dimension has its own block)
    for (unsigned int d = 0; d < numVariables; d++)
    {
        // Calculate left and right products
        int leftProd = 1;
        int rightProd = 1;
        for (unsigned int k = 0; k < d; k++)
        {
            leftProd *= dims[k];
        }
        for (unsigned int k = d+1; k < numVariables; k++)
        {
            rightProd *= dims[k];
        }

        // Loop through subblocks on the block diagonal
        for (int j = 0; j < rightProd; j++)
        {
            // Start column of current subblock
            int blkBaseCol = j*leftProd*dims[d];
            // Block rows [I -2I I] of subblock
            for (unsigned int l = 0; l < (dims[d] - 2); l++)
            {
                // Special case for first dimension
                if (d == 0)
                {
                    int k = j*leftProd*dims[d] + l;
                    D.insert(i,k) = 1;
                    k += leftProd;
                    D.insert(i,k) = -2;
                    k += leftProd;
                    D.insert(i,k) = 1;
                    i++;
                }
                else
                {
                    // Loop for identity matrix
                    for (int n = 0; n < leftProd; n++)
                    {
                        int k = blkBaseCol + l*leftProd + n;
                        D.insert(i,k) = 1;
                        k += leftProd;
                        D.insert(i,k) = -2;
                        k += leftProd;
                        D.insert(i,k) = 1;
                        i++;
                    }
                }
            }
        }
    }

    D.makeCompressed();

    return D;
}

// Compute weight matrix for P-splines
SparseMatrix BSpline::Builder::getWeightMatrix() const
{
    unsigned int numSamples = _data.getNumSamples();

    SparseMatrix W;
    W.resize(numSamples, numSamples);
    if (_weights.size() == 0) {
        // no weights given, use the identity matrix
        W.setIdentity();
    } else {
        // user set weights use those
        W.reserve(Eigen::VectorXi::Constant(numSamples, 1));
        for(unsigned int i = 0; i < numSamples; i++) {
            W.insert(i,i) = _weights[i];
        }
    }

    return W;
}


// Compute all knot vectors from sample data
std::vector<std::vector<double> > BSpline::Builder::computeKnotVectors() const
{
    if (_data.getNumVariables() != _degrees.size())
        throw Exception("BSpline::Builder::computeKnotVectors: Inconsistent sizes on input vectors.");

    std::vector<std::vector<double>> grid = _data.getTableX();

    std::vector<std::vector<double>> knotVectors;

    for (unsigned int i = 0; i < _data.getNumVariables(); ++i)
    {
        // Compute knot vector
        std::array<double, 2> bounds = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
        if (_bounds.size() > 0) {
            bounds = _bounds.at(i);
        }

        auto knotVec = computeKnotVector(grid.at(i), _degrees.at(i), _numBasisFunctions.at(i), bounds);

        knotVectors.push_back(knotVec);
    }

    return knotVectors;
}

// Compute a single knot vector from sample grid and degree
std::vector<double> BSpline::Builder::computeKnotVector(const std::vector<double> &values,
                                                        unsigned int degree,
                                                        unsigned int numBasisFunctions,
                                                        std::array<double, 2> bounds) const
{
    switch (_knotSpacing)
    {
        case KnotSpacing::AS_SAMPLED:
            return knotVectorMovingAverage(values, degree);
        case KnotSpacing::EQUIDISTANT:
            return knotVectorEquidistant(values, degree, numBasisFunctions, bounds);
        case KnotSpacing::EXPERIMENTAL:
            return knotVectorBuckets(values, degree);
        default:
            return knotVectorMovingAverage(values, degree);
    }
}

/*
* Automatic construction of (p+1)-regular knot vector
* using moving average.
*
* Requirement:
* Knot vector should be of size n+p+1.
* End knots are should be repeated p+1 times.
*
* Computed sizes:
* n+2*(p) = n + p + 1 + (p - 1)
* k = (p - 1) values must be removed from sample vector.
* w = k + 3 window size in moving average
*
* Algorithm:
* 1) compute n - k values using moving average with window size w
* 2) repeat first and last value p + 1 times
*
* The resulting knot vector has n - k + 2*p = n + p + 1 knots.
*
* NOTE:
* For equidistant samples, the resulting knot vector is identically to
* the free end conditions knot vector used in cubic interpolation.
* That is, samples (a,b,c,d,e,f) produces the knot vector (a,a,a,a,c,d,f,f,f,f) for p = 3.
* For p = 1, (a,b,c,d,e,f) becomes (a,a,b,c,d,e,f,f).
*
* TODO:
* Does not work well when number of knots is << number of samples! For such cases
* almost all knots will lie close to the left samples. Try a bucket approach, where the
* samples are added to buckets and the knots computed as the average of these.
*/
std::vector<double> BSpline::Builder::knotVectorMovingAverage(const std::vector<double> &values,
                                                              unsigned int degree) const
{
    // Sort and remove duplicates
    std::vector<double> unique = extractUniqueSorted(values);

    // Compute sizes
    unsigned int n = unique.size();
    unsigned int k = degree-1; // knots to remove
    unsigned int w = k + 3; // Window size

    // The minimum number of samples from which a free knot vector can be created
    if (n < degree+1)
    {
        std::ostringstream e;
        e << "knotVectorMovingAverage: Only " << n
        << " unique interpolation points are given. A minimum of degree+1 = " << degree+1
        << " unique points are required to build a B-spline basis of degree " << degree << ".";
        throw Exception(e.str());
    }

    std::vector<double> knots(n-k-2, 0);

    // Compute (n-k-2) interior knots using moving average
    for (unsigned int i = 0; i < n-k-2; ++i)
    {
        double ma = 0;
        for (unsigned int j = 0; j < w; ++j)
            ma += unique.at(i+j);

        knots.at(i) = ma/w;
    }

    // Repeat first knot p + 1 times (for interpolation of start point)
    for (unsigned int i = 0; i < degree + 1; ++i)
        knots.insert(knots.begin(), unique.front());

    // Repeat last knot p + 1 times (for interpolation of end point)
    for (unsigned int i = 0; i < degree + 1; ++i)
        knots.insert(knots.end(), unique.back());

    // Number of knots in a (p+1)-regular knot vector
    //assert(knots.size() == uniqueX.size() + degree + 1);

    return knots;
}

std::vector<double> BSpline::Builder::knotVectorEquidistant(const std::vector<double> &values,
                                                            unsigned int degree,
                                                            unsigned int numBasisFunctions,
                                                            std::array<double, 2> bounds) const
{
    // Sort and remove duplicates
    std::vector<double> unique = extractUniqueSorted(values);

    // Compute sizes
    unsigned int n = unique.size();
    if (numBasisFunctions > 0)
        n = numBasisFunctions;
    unsigned int k = degree-1; // knots to remove

    // The minimum number of samples from which a free knot vector can be created
    if (n < degree+1)
    {
        std::ostringstream e;
        e << "knotVectorMovingAverage: Only " << n
        << " unique interpolation points are given. A minimum of degree+1 = " << degree+1
        << " unique points are required to build a B-spline basis of degree " << degree << ".";
        throw Exception(e.str());
    }

    // Compute boundaries
    double lo = (std::isnan(bounds[0]) ? unique.front() : bounds[0]);
    double hi = (std::isnan(bounds[1]) ? unique.back() : bounds[1]);
    double pad = (hi - lo) * _padding;
    lo -= pad;
    hi += pad;

    // Compute (n-k-2) equidistant interior knots
    unsigned int numIntKnots = std::max(n-k-2, (unsigned int)0);
    // numIntKnots = std::min((unsigned int)10, numIntKnots);
    std::vector<double> knots = linspace(lo, hi, numIntKnots);

    // Repeat first knot p + 1 times (for interpolation of start point)
    for (unsigned int i = 0; i < degree; ++i)
        knots.insert(knots.begin(), lo);

    // Repeat last knot p + 1 times (for interpolation of end point)
    for (unsigned int i = 0; i < degree; ++i)
        knots.insert(knots.end(), hi);

    // Number of knots in a (p+1)-regular knot vector
    //assert(knots.size() == uniqueX.size() + degree + 1);

    return knots;
}

std::vector<double> BSpline::Builder::knotVectorBuckets(const std::vector<double> &values, unsigned int degree, unsigned int maxSegments) const
{
    // Sort and remove duplicates
    std::vector<double> unique = extractUniqueSorted(values);

    // The minimum number of samples from which a free knot vector can be created
    if (unique.size() < degree+1)
    {
        std::ostringstream e;
        e << "BSpline::Builder::knotVectorBuckets: Only " << unique.size()
        << " unique sample points are given. A minimum of degree+1 = " << degree+1
        << " unique points are required to build a B-spline basis of degree " << degree << ".";
        throw Exception(e.str());
    }

    // Num internal knots (0 <= ni <= unique.size() - degree - 1)
    unsigned int ni = unique.size() - degree - 1;

    // Num segments
    unsigned int ns = ni + degree + 1;

    // Limit number of segments
    if (ns > maxSegments && maxSegments >= degree + 1)
    {
        ns = maxSegments;
        ni = ns - degree - 1;
    }

    // Num knots
//        unsigned int nk = ns + degree + 1;

    // Check numbers
    if (ni > unique.size() - degree - 1)
        throw Exception("BSpline::Builder::knotVectorBuckets: Invalid number of internal knots!");

    // Compute window sizes
    unsigned int w = 0;
    if (ni > 0)
        w = std::floor(unique.size()/ni);

    // Residual
    unsigned int res = unique.size() - w*ni;

    // Create array with window sizes
    std::vector<unsigned int> windows(ni, w);

    // Add residual
    for (unsigned int i = 0; i < res; ++i)
        windows.at(i) += 1;

    // Compute internal knots
    std::vector<double> knots(ni, 0);

    // Compute (n-k-2) interior knots using moving average
    unsigned int index = 0;
    for (unsigned int i = 0; i < ni; ++i)
    {
        for (unsigned int j = 0; j < windows.at(i); ++j)
        {
            knots.at(i) += unique.at(index+j);
        }
        knots.at(i) /= windows.at(i);
        index += windows.at(i);
    }

    // Repeat first knot p + 1 times (for interpolation of start point)
    for (unsigned int i = 0; i < degree + 1; ++i)
        knots.insert(knots.begin(), unique.front());

    // Repeat last knot p + 1 times (for interpolation of end point)
    for (unsigned int i = 0; i < degree + 1; ++i)
        knots.insert(knots.end(), unique.back());

    return knots;
}

std::vector<double> BSpline::Builder::extractUniqueSorted(const std::vector<double> &values) const
{
    // Sort and remove duplicates
    std::vector<double> unique(values);
    std::sort(unique.begin(), unique.end());
    std::vector<double>::iterator it = unique_copy(unique.begin(), unique.end(), unique.begin());
    unique.resize(distance(unique.begin(),it));
    return unique;
}

} // namespace SPLINTER