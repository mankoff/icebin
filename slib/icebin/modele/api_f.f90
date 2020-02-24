! IceBin: A Coupling Library for Ice Models and GCMs
! Copyright (c) 2013-2016 by Elizabeth Fischer
! 
! This program is free software: you can redistribute it and/or modify
! it under the terms of the GNU Lesser General Public License as published
! by the Free Software Foundation, either version 3 of the License, or
! (at your option) any later version.
! 
! This program is distributed in the hope that it will be useful,
! but WITHOUT ANY WARRANTY; without even the implied warranty of
! MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
! GNU Lesser General Public License for more details.
! 
! You should have received a copy of the GNU Lesser General Public License
! along with this program.  If not, see <http://www.gnu.org/licenses/>.

module icebin_modele
use icebin_f90blitz
use iso_c_binding
implicit none





! Parameters read out of the ModelE rundeck and sent to IceBin
! These are later incorporated in gcmce_new()
integer, parameter :: MAX_CHAR_LEN = 128   ! From ModelE's Dictionary_mod.F90
type, bind(c) :: ModelEParams_t

    ! Segment specs: to be further parsed.
!    character(MAX_CHAR_LEN, kind=c_char) :: icebin_segments = 'legacy,sealand,ec'
!    real(c_double) :: dtsrc
!    integer(c_int) :: yeari
!    integer(c_int) :: dummy

    integer :: istart
end type ModelEParams_t


! Similar to C++ class VectorSparse
! Represents a sparse matrix as a set of vectors
type :: VectorSparse_t
    ! indices of row and col in whatever form is desired for a particular purpose
    ! There could be 2 or more indices per value
    integer(c_int), pointer :: indices(:)
    ! Value of non-zero elements
    real(c_double), pointer :: values(:)
contains
    procedure :: from_c
end type VectorSparse_t
! ---------------------------------------------------------

! ================================================
! Stuff from icebin_modele.cpp

INTERFACE

    ! ------------------------------------------------------
    ! ------------- LISheetIceBin::allocate()


    ! Called from lisheeticebin%allocate()
    function gcmce_new( &
        rdparams, &
        im,jm, &
        i0,i1,j0,j1, &
        comm_f, root) bind(c)
    use iso_c_binding
    import ModelEParams_t
        type(c_ptr) :: gcmce_new
        type(ModelEParams_t) :: rdparams
        integer(c_int), value :: im, jm
        integer(c_int), value :: i0,i1,j0,j1
        integer(c_int), value :: comm_f, root
    end function gcmce_new


    ! Called from lisheeticebin%allocate() (via setup_gcm_inputs)
    subroutine gcmce_hc_params(api, nhc_gcm, icebin_base_hc, nhc_ice) bind(c)
    use iso_c_binding
        type(c_ptr), value :: api
        integer(c_int) :: gcmce_nhc_gcm, icebin_base_hc, nhc_ice
    end subroutine gcmce_hc_params

    ! Must conform to the set_constant_cb() interface
    ! See ExportConstants.F90 (ModelE); generated by write_export_constants.py
    ! Called from lisheeticebin%allocate()
    subroutine gcmce_set_constant(api, &
        name_f, name_len, &
        val, &
        units_f, units_len, &
        description_f, description_len) bind(c)
    use iso_c_binding
        type(c_ptr), value :: api
        character(c_char) :: name_f(*)
        integer(c_int), value :: name_len
        real(c_double), value :: val
        character(c_char) :: units_f(*)
        integer(c_int), value :: units_len
        character(c_char) :: description_f(*)
        integer(c_int), value :: description_len
    end subroutine gcmce_set_constant

    subroutine gcmce_add_gcm_outputE( &
        api, var_f, &
        field_f, field_len, &
        units_f, units_len, &
        ncunits_f, ncunits_len, &
        mm, bb, &
        long_name_f, long_name_len) bind(c)
    use iso_c_binding
    use icebin_f90blitz
        type(c_ptr), value :: api
        type(arr_spec_3) :: var_f
        character(c_char) :: field_f(*)
        integer(c_int), value :: field_len
        character(c_char) :: units_f(*), ncunits_f(*)
        integer(c_int), value :: units_len, ncunits_len
        real(c_double), value :: mm, bb
        character(c_char) :: long_name_f(*)
        integer(c_int), value :: long_name_len
    end subroutine gcmce_add_gcm_outputE

    ! Called from lisheeticebin%allocate() (via setup_gcm_inputs())
    subroutine gcmce_add_gcm_inputA( &
        api, index_ae, var_f, &
        field_f, field_len, &
        units_f, units_len, &
        ncunits_f, ncunits_len, &
        mm, bb, &
        initial, &
        long_name_f, long_name_len) bind(c)
    use iso_c_binding
    use icebin_f90blitz
        type(c_ptr), value :: api
        integer(c_int), value :: index_ae
        type(arr_spec_2) :: var_f
        character(c_char) :: field_f(*)
        integer(c_int), value :: field_len
        character(c_char) :: units_f(*), ncunits_f(*)
        integer(c_int), value :: units_len, ncunits_len
        real(c_double), value :: mm, bb
        character(c_char) :: long_name_f(*)
        logical(c_bool), value :: initial
        integer(c_int), value :: long_name_len
    end subroutine gcmce_add_gcm_inputA

    ! Called from lisheeticebin%allocate() (via setup_gcm_inputs())
    subroutine gcmce_add_gcm_inputE( &
        api, index_ae, var_f, &
        field_f, field_len, &
        units_f, units_len, &
        ncunits_f, ncunits_len, &
        mm, bb, &
        initial, &
        long_name_f, long_name_len) bind(c)
    use iso_c_binding
    use icebin_f90blitz
        type(c_ptr), value :: api
        integer(c_int), value :: index_ae
        type(arr_spec_3) :: var_f
        character(c_char) :: field_f(*)
        integer(c_int), value :: field_len
        character(c_char) :: units_f(*), ncunits_f(*)
        integer(c_int), value :: units_len, ncunits_len
        real(c_double), value :: mm, bb
        logical(c_bool), value :: initial
        character(c_char) :: long_name_f(*)
        integer(c_int), value :: long_name_len
    end subroutine gcmce_add_gcm_inputE

    subroutine gcmce_reference_globals(api, &
        fhc, underice, elevE, &
        fland, focean, flake, fgrnd, fgice, zatmo, zlake) bind(c)
    use iso_c_binding
    use icebin_f90blitz
        type(c_ptr), value :: api
        type(arr_spec_3) :: fhc, underice, elevE
        type(arr_spec_2) :: fland, focean, flake, fgrnd, fgice, zatmo, zlake
    end subroutine gcmce_reference_globals

    subroutine gcmce_write_rsf(api, &
        modele_fname_f, modele_fname_len) bind(c)
    use iso_c_binding
        type(c_ptr), value :: api
        character(c_char) :: modele_fname_f(*)
        integer(c_int), value :: modele_fname_len
    end subroutine gcmce_write_rsf

    subroutine gcmce_couple_native(api, itime, &
        run_ice, E1vE0c_indices, E1vE0c_values, E1vE0c_nele) bind(c)
    use iso_c_binding
    use icebin_f90blitz
        type(c_ptr), value :: api
        integer(c_int), value :: itime
        logical(c_bool), value :: run_ice
        type(c_ptr) :: E1vE0c_indices
        type(c_ptr) :: E1vE0c_values
        integer :: E1vE0c_nele
    end subroutine

    subroutine gcmce_cold_start(api, cold_start, yeari, itimei, dtsrc) bind(c)
        use iso_c_binding
        type(c_ptr), value :: api
        type(c_bool), value :: cold_start
        integer(c_int), value :: yeari
        integer(c_int), value :: itimei
        real(c_double), value :: dtsrc
    end subroutine


END INTERFACE

CONTAINS

! Extract arrays from C pointers returned by C function
subroutine from_c(self, indices_c, values_c, nnz)
    class(VectorSparse_t) :: self
    type(c_ptr), value :: indices_c, values_c
    integer, value :: nnz

    ! Convert E1vE0 matrix, returned by gcmce_couple_native(), to
    ! Fortran arrays.
    call c_f_pointer(indices_c, self%indices, [nnz*4])
    call c_f_pointer(values_c, self%values, [nnz])
end subroutine

end module
