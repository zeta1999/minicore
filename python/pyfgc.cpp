#include "pyfgc.h"


PYBIND11_MODULE(pyfgc, m) {
    init_ex1(m);
    m.doc() = "Python bindings for FGC, which allows for calling coreset/clustering code from numpy and converting results back to numpy arrays";
}
