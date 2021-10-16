package splinter

// #cgo CXXFLAGS: -std=c++11 -I../include  -Werror=return-type -DSPLINTER_ALLOW_SCATTER
// #cgo CFLAGS: -I../include  -Werror=return-type -DSPLINTER_ALLOW_SCATTER
// #cgo pkg-config: eigen3
// #include <cinterface/cinterface.h>
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
