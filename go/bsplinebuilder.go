package splinter

// #cgo CXXFLAGS: -std=c++11 -I../include  -Werror=return-type
// #cgo CFLAGS: -I../include  -Werror=return-type
// #cgo pkg-config: eigen3
// #include <cinterface/cinterface.h>
import "C"

import (
	"errors"
	"unsafe"
)

var (
	ErrInvalidNil = errors.New("Expected an object, got a nil")
)

type BSplineBuilder struct {
	ptr unsafe.Pointer
}

type DataTable struct {
	ptr unsafe.Pointer
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
	res.ptr = unsafe.Pointer(ptr)
	return res, nil
}

func NewBSplineBuilder(table *DataTable) (*BSplineBuilder, error) {
	if table == nil {
		return nil, ErrInvalidNil
	}

	ptr := C.splinter_bspline_builder_init(C.splinter_obj_ptr(table.ptr))
	err := getErrorIfExists()
	if err != nil {
		// make sure we clean up if we got a pointer and an error
		if ptr != nil {
			C.splinter_bspline_builder_delete(ptr)
		}

		return nil, err
	}

	res := new(BSplineBuilder)
	res.ptr = unsafe.Pointer(ptr)
	return res, nil
}
