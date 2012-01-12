cdef extern from "config.h":
    pass

cdef extern from "maggs.h":
    ctypedef struct MAGGS_struct:
        double f_mass
        double bjerrum
        int mesh
        double invsqrt_f_mass

    MAGGS_struct maggs
    int maggs_set_parameters(double bjerrum, double f_mass, int mesh)
