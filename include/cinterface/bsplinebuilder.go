package splinter

// #cgo CXXFLAGS: -std=c++11  -Werror=return-type -DSPLINTER_ALLOW_SCATTER
// #cgo CFLAGS: -Werror=return-type -DSPLINTER_ALLOW_SCATTER
// #cgo LDFLAGS: -lsplinter-static-3-0 -lstdc++ -lm
// #include "cinterface.h"
// #include <stdlib.h>
import "C"

import (
	"errors"
	"runtime"
	"unsafe"
)

var (
	ErrInvalidNil        = errors.New("Expected an object, got a nil")
	ErrLengthMismatch    = errors.New("Input slices must be of the same size")
	ErrGotNullPtr        = errors.New("Unexpected NULL return from call")
	ErrZeroVariables     = errors.New("BSpline returned variable dimension set to 0")
	ErrDimensionMismatch = errors.New("Input dimension not equal to BSpline's")
	ErrInvalidBounds     = errors.New("Bounds should contain min and max boundaries (exactly two elements)")
)

type KnotSpacing int

const (
	KnotSpacingAsSampled    KnotSpacing = 0
	KnotSpacingEquidistant              = 1
	KnotSpacingExperimental             = 2
)

type Smoothing int

const (
	SmoothingNone     Smoothing = 0
	SmoothingIdentity           = 1
	SmoothingPspline            = 2
)

type BSplineBuilder struct {
	ptr C.splinter_obj_ptr
}

type DataTable struct {
	ptr C.splinter_obj_ptr
}

type BSpline struct {
	ptr C.splinter_obj_ptr
}

// getErrorIfExists checks splinter for an error in the last call, and returns an error if one happened, nil otherwise.
func getErrorIfExists() error {
	if C.splinter_get_error() == 1 {
		return errors.New(C.GoString(C.splinter_get_error_string()))
	}
	return nil
}

////////////////////
//// DataTable
////////////////////

func NewDataTable() (*DataTable, error) {
	ptr := C.splinter_datatable_init()
	err := getErrorIfExists()
	if err != nil {
		// make sure we clean up if we got a pointer and an error
		if ptr != nil {
			C.splinter_datatable_delete(ptr)
		}

		return nil, err
	}

	res := new(DataTable)
	res.ptr = ptr
	runtime.SetFinalizer(res, func(dt *DataTable) { C.splinter_datatable_delete(dt.ptr) })
	return res, nil
}

func (dt *DataTable) Free() {
	runtime.SetFinalizer(dt, nil)
	C.splinter_datatable_delete(dt.ptr)
	dt.ptr = nil
}

// AddColumns adds the given columns to the datatable.
// The columns must be the same length, otherwise returns ErrLengthMismatch
func (dt DataTable) AddColumns(columns ...[]float64) error {
	// if user didn't add anything, return nil
	if len(columns) == 0 {
		return nil
	}

	// check the lengths of columns -- should all be identical
	n := len(columns[0])
	for i := 1; i < len(columns); i++ {
		if len(columns[i]) != n {
			return ErrLengthMismatch
		}
	}

	// we will concatenate the columns and use `splinter_datatable_add_samples_col_major`
	concat := make([]float64, 0, n*len(columns))
	for _, col := range columns {
		concat = append(concat, col...)
	}

	// now add the samples
	C.splinter_datatable_add_samples_col_major(dt.ptr, (*C.double)(unsafe.Pointer(&concat[0])),
		C.int(n), C.int(len(columns)-1))
	return getErrorIfExists()
}

////////////////////
//// BSplineBuilder
////////////////////

func NewBSplineBuilder(table *DataTable) (*BSplineBuilder, error) {
	if table == nil {
		return nil, ErrInvalidNil
	}

	ptr := C.splinter_bspline_builder_init(table.ptr)
	err := getErrorIfExists()
	if err != nil {
		// make sure we clean up if we got a pointer and an error
		if ptr != nil {
			C.splinter_bspline_builder_delete(ptr)
		}

		return nil, err
	}

	res := new(BSplineBuilder)
	res.ptr = ptr
	runtime.SetFinalizer(res, func(builder *BSplineBuilder) { C.splinter_bspline_builder_delete(builder.ptr) })
	return res, nil
}

func (builder *BSplineBuilder) Free() {
	runtime.SetFinalizer(builder, nil)
	C.splinter_bspline_builder_delete(builder.ptr)
	builder.ptr = nil
}

func (builder *BSplineBuilder) KnotSpacing(ks KnotSpacing) error {
	C.splinter_bspline_builder_set_knot_spacing(builder.ptr, C.int(ks))
	return getErrorIfExists()
}

func (builder *BSplineBuilder) Smoothing(s Smoothing) error {
	C.splinter_bspline_builder_set_smoothing(builder.ptr, C.int(s))
	return getErrorIfExists()
}

func (builder *BSplineBuilder) Alpha(alpha float64) error {
	C.splinter_bspline_builder_set_alpha(builder.ptr, C.double(alpha))
	return getErrorIfExists()
}

func (builder *BSplineBuilder) Padding(padding float64) error {
	C.splinter_bspline_builder_set_padding(builder.ptr, C.double(padding))
	return getErrorIfExists()
}

func (builder *BSplineBuilder) Weights(weights []float64) error {
	C.splinter_bspline_builder_set_weights(builder.ptr, (*C.double)(&weights[0]), C.int(len(weights)))
	return getErrorIfExists()
}

func (builder *BSplineBuilder) Bounds(bounds [][]float64) error {
	minBounds := make([]float64, len(bounds))
	maxBounds := make([]float64, len(bounds))
	for i, b := range bounds {
		if len(b) != 2 {
			return ErrInvalidBounds
		}
		minBounds[i] = b[0]
		maxBounds[i] = b[1]
	}

	C.splinter_bspline_builder_set_bounds(builder.ptr, (*C.double)(&minBounds[0]), (*C.double)(&maxBounds[0]), C.int(len(bounds)))
	return getErrorIfExists()
}

func (builder *BSplineBuilder) HfsIters(iters uint) error {
	C.splinter_bspline_builder_set_hfs_iters(builder.ptr, C.uint(iters))
	return getErrorIfExists()
}

func (builder *BSplineBuilder) NumBasisFunctions(n []int) error {

	// Convert to C.uint
	nC := make([]C.int, len(n))
	for i, x := range n {
		nC[i] = C.int(x)
	}

	C.splinter_bspline_builder_set_num_basis_functions(builder.ptr, &nC[0], C.int(len(nC)))
	return getErrorIfExists()
}

func (builder *BSplineBuilder) Build() (*BSpline, error) {
	ptr := C.splinter_bspline_builder_build(builder.ptr)
	err := getErrorIfExists()
	if err != nil {
		// make sure we clean up if we got a pointer and an error
		if ptr != nil {
			C.splinter_bspline_delete(ptr)
		}

		return nil, err
	}

	res := new(BSpline)
	res.ptr = ptr
	runtime.SetFinalizer(res, func(bs *BSpline) { C.splinter_bspline_delete(bs.ptr) })
	return res, nil
}

/////////////
//// BSpline
/////////////

func (bs *BSpline) Free() {
	runtime.SetFinalizer(bs, nil)
	C.splinter_bspline_delete(bs.ptr)
	bs.ptr = nil
}

func (bs *BSpline) Eval(vals ...float64) (float64, error) {
	n := C.splinter_bspline_get_num_variables(bs.ptr)
	if n == 0 {
		return 0, ErrZeroVariables
	}

	if len(vals) != int(n) {
		return 0, ErrDimensionMismatch
	}

	arr := C.splinter_bspline_eval_row_major(bs.ptr, (*C.double)(unsafe.Pointer(&vals[0])), C.int(len(vals)))
	defer C.free(unsafe.Pointer(arr))

	err := getErrorIfExists()
	if err != nil {
		return 0, err
	}

	if arr == nil {
		return 0, ErrGotNullPtr
	}

	return *(*float64)(unsafe.Pointer(arr)), nil
}

func (bs *BSpline) GetCoefficients() ([]float64, error) {
	n := C.splinter_bspline_get_num_coefficients(bs.ptr)
	if n < 0 {
		return nil, getErrorIfExists()
	}

	arr := C.splinter_bspline_get_coefficients(bs.ptr)
	if arr == nil {
		return nil, getErrorIfExists()
	}
	defer C.free(unsafe.Pointer(arr))

	// to be on the safe side that values are valid, check error explicitly even if we got a non-nil
	err := getErrorIfExists()
	if err != nil {
		return nil, err
	}

	// based on https://github.com/golang/go/wiki/cgo#turning-c-arrays-into-go-slices
	coeffInCMemory := (*[1 << 28]float64)(unsafe.Pointer(arr))[:n:n]

	// make our own slice
	coeff := make([]float64, 0, n)

	// append the elements from the C slice to golang-managed memory
	coeff = append(coeff, coeffInCMemory...)

	return coeff, nil
}
