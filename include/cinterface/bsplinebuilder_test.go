package splinter

import (
	"testing"
)

func TestBSplinebuilder(t *testing.T) {
	dt, err := NewDataTable()
	if err != nil {
		t.Fatal(err)
	}

	builder, err := NewBSplineBuilder(dt)
	if err != nil {
		t.Fatal(err)
	}

	_ = builder
}
