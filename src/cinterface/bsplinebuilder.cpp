/*
 * This file is part of the SPLINTER library.
 * Copyright (C) 2012 Bjarne Grimstad (bjarne.grimstad@gmail.com).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "bsplinebuilder.h"
#include "cinterface/cinterface.h"
#include "cinterface/utilities.h"
#include <array>
//#include <fstream>

using namespace SPLINTER;

extern "C"
{

splinter_obj_ptr splinter_bspline_builder_init(splinter_obj_ptr datatable_ptr)
{
    splinter_obj_ptr bspline_builder_ptr = nullptr;

    try
    {
        DataTable *dataTable = get_datatable(datatable_ptr);
        bspline_builder_ptr = new BSpline::Builder(*dataTable);
#ifdef SPLINTER_CINTERFACE_SINGLE_THREADED_ALLOC_CHECK
        bspline_builders.insert(bspline_builder_ptr);
#endif
    }
    catch (const Exception &e)
    {
        set_error_string(e.what());
    }

    return bspline_builder_ptr;
}

void splinter_bspline_builder_set_degree(splinter_obj_ptr bspline_builder_ptr, unsigned int *degrees, int n)
{
    auto builder = get_builder(bspline_builder_ptr);
    if(builder != nullptr)
    {
        try {
            auto _degrees = get_vector(degrees, n);
            builder->degree(_degrees);
        } catch (const Exception &e) {
            set_error_string(e.what());
        }
    }
}

void splinter_bspline_builder_set_num_basis_functions(splinter_obj_ptr bspline_builder_ptr, int *num_basis_functions, int n)
{
    auto builder = get_builder(bspline_builder_ptr);
    if(builder != nullptr)
    {
        try {
            std::vector<unsigned int> _num_basis_functions((unsigned int) n);
            for (int i = 0; i < n; ++i)
            {
                _num_basis_functions.at(i) = (unsigned int) num_basis_functions[i];
            }
            builder->numBasisFunctions(_num_basis_functions);
        } catch (const Exception &e) {
            set_error_string(e.what());
        }
    }
}

void splinter_bspline_builder_set_knot_spacing(splinter_obj_ptr bspline_builder_ptr, int knot_spacing)
{
    auto builder = get_builder(bspline_builder_ptr);
    if(builder != nullptr)
    {
        switch (knot_spacing)
        {
            case 0:
                builder->knotSpacing(BSpline::KnotSpacing::AS_SAMPLED);
                break;
            case 1:
                builder->knotSpacing(BSpline::KnotSpacing::EQUIDISTANT);
                break;
            case 2:
                builder->knotSpacing(BSpline::KnotSpacing::EXPERIMENTAL);
                break;
            default:
                set_error_string("Error: Invalid knot spacing!");
                break;
        }
    }
}

void splinter_bspline_builder_set_smoothing(splinter_obj_ptr bspline_builder_ptr, int smoothing)
{
    auto builder = get_builder(bspline_builder_ptr);
    if(builder != nullptr)
    {
        switch (smoothing)
        {
            case 0:
                builder->smoothing(BSpline::Smoothing::NONE);
                break;
            case 1:
                builder->smoothing(BSpline::Smoothing::IDENTITY);
                break;
            case 2:
                builder->smoothing(BSpline::Smoothing::PSPLINE);
                break;
            default:
                set_error_string("Error: Invalid smoothing!");
                break;
        }
    }
}

void splinter_bspline_builder_set_alpha(splinter_obj_ptr bspline_builder_ptr, double alpha)
{
    auto builder = get_builder(bspline_builder_ptr);
    if (builder == nullptr)
    {
        // Error string will have been set by get_builder
        return;
    }

    try {
        builder->alpha(alpha);
    } catch (const Exception &e) {
        set_error_string(e.what());
    }
}

void splinter_bspline_builder_set_padding(splinter_obj_ptr bspline_builder_ptr, double padding)
{
    auto builder = get_builder(bspline_builder_ptr);
    if (builder == nullptr)
    {
        // Error string will have been set by get_builder
        return;
    }

    try {
        builder->padding(padding);
    } catch (const Exception &e) {
        set_error_string(e.what());
    }
}

void splinter_bspline_builder_set_weights(splinter_obj_ptr bspline_builder_ptr, double *weights, int n)
{
    auto builder = get_builder(bspline_builder_ptr);
    if (builder == nullptr)
    {
        // Error string will have been set by get_builder
        return;
    }

    try {
        std::vector<double> wvec(weights, weights + n);
        builder->weights(wvec);
    } catch (const Exception &e) {
        set_error_string(e.what());
    }
}

void splinter_bspline_builder_set_bounds(splinter_obj_ptr bspline_builder_ptr, double *min_bounds, double *max_bounds, int n)
{
    auto builder = get_builder(bspline_builder_ptr);
    if (builder == nullptr)
    {
        // Error string will have been set by get_builder
        return;
    }

    try {
        std::vector<std::array<double,2> > bounds(n);
        for (int i = 0; i < n; i++) {
            bounds[i] = {min_bounds[i], max_bounds[i]};
        }

        builder->bounds(bounds);
    } catch (const Exception &e) {
        set_error_string(e.what());
    }
}

SPLINTER_API void splinter_bspline_builder_set_hfs_iters(splinter_obj_ptr bspline_builder_ptr, unsigned int iters)
{
    auto builder = get_builder(bspline_builder_ptr);
    if (builder == nullptr)
    {
        // Error string will have been set by get_builder
        return;
    }

    try {
        builder->hfsIters(iters);
    } catch (const Exception &e) {
        set_error_string(e.what());
    }
}

splinter_obj_ptr splinter_bspline_builder_build(splinter_obj_ptr bspline_builder_ptr)
{
    auto builder = get_builder(bspline_builder_ptr);
    if (builder == nullptr)
    {
        return nullptr;
    }

    try
    {
        auto bspline = builder->build().clone();
#ifdef SPLINTER_CINTERFACE_SINGLE_THREADED_ALLOC_CHECK
        bsplines.insert(bspline);
#endif
        return bspline;
    }
    catch (const Exception &e)
    {
        set_error_string(e.what());
        return nullptr;
    }
    // unreachable
}

void splinter_bspline_builder_delete(splinter_obj_ptr bspline_builder_ptr)
{
    auto builder = get_builder(bspline_builder_ptr);
    if (builder == nullptr)
    {
        return;
    }

    delete builder;
}

} // extern "C"
