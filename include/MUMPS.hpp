/*
   This file is part of HPDDM.

   Author(s): Pierre Jolivet <pierre.jolivet@enseeiht.fr>
        Date: 2012-10-04

   Copyright (C) 2011-2014 Université de Grenoble
                 2015      Eidgenössische Technische Hochschule Zürich
                 2016-     Centre National de la Recherche Scientifique

   HPDDM is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published
   by the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   HPDDM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with HPDDM.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _HPDDM_MUMPS_
#define _HPDDM_MUMPS_

#include <smumps_c.h>
#include <dmumps_c.h>
#include <cmumps_c.h>
#include <zmumps_c.h>

#define HPDDM_GENERATE_MUMPS(C, c, T, M)                                                                     \
template<>                                                                                                   \
struct MUMPS_STRUC_C<T> {                                                                                    \
    typedef C ## MUMPS_STRUC_C trait;                                                                        \
    typedef M mumps_type;                                                                                    \
    static void mumps_c(C ## MUMPS_STRUC_C* const id) { c ## mumps_c(id); }                                  \
};

namespace HPDDM {
template<class>
struct MUMPS_STRUC_C { };
HPDDM_GENERATE_MUMPS(S, s, float, float)
HPDDM_GENERATE_MUMPS(D, d, double, double)
HPDDM_GENERATE_MUMPS(C, c, std::complex<float>, mumps_complex)
HPDDM_GENERATE_MUMPS(Z, z, std::complex<double>, mumps_double_complex)

#ifdef DMUMPS
#undef HPDDM_CHECK_SUBDOMAIN
#define HPDDM_CHECK_COARSEOPERATOR
#include "preprocessor_check.hpp"
#define COARSEOPERATOR HPDDM::Mumps
/* Class: Mumps
 *
 *  A class inheriting from <DMatrix> to use <Mumps>.
 *
 * Template Parameter:
 *    K              - Scalar type. */
template<class K>
class Mumps : public DMatrix {
    private:
        /* Variable: id
         *  Internal data pointer. */
        typename MUMPS_STRUC_C<K>::trait* _id;
    protected:
        /* Variable: numbering
         *  1-based indexing. */
        static constexpr char _numbering = 'F';
    public:
        Mumps() : _id() { }
        ~Mumps() {
            if(_id) {
                _id->job = -2;
                MUMPS_STRUC_C<K>::mumps_c(_id);
                delete _id;
            }
        }
        /* Function: numfact
         *
         *  Initializes <Mumps::id> and factorizes the supplied matrix.
         *
         * Template Parameter:
         *    S              - 'S'ymmetric or 'G'eneral factorization.
         *
         * Parameters:
         *    nz             - Number of nonzero entries.
         *    I              - Array of row indices.
         *    J              - Array of column indices.
         *    C              - Array of data. */
        template<char S>
        void numfact(unsigned int nz, int* I, int* J, K* C) {
            _id = new typename MUMPS_STRUC_C<K>::trait();
            _id->job = -1;
            _id->par = 1;
            _id->comm_fortran = MPI_Comm_c2f(DMatrix::_communicator);
            const Option& opt = *Option::get();
            if(S == 'S')
                _id->sym = opt.val<char>("master_not_spd", 0) ? 2 : 1;
            else
                _id->sym = 0;
            MUMPS_STRUC_C<K>::mumps_c(_id);
            _id->n = _id->lrhs = DMatrix::_n;
            _id->nz_loc = nz;
            _id->irn_loc = I;
            _id->jcn_loc = J;
            _id->a_loc = reinterpret_cast<typename MUMPS_STRUC_C<K>::mumps_type*>(C);
            _id->nrhs = 1;
            _id->icntl[4] = 0;
            for(unsigned short i = 5; i < 40; ++i) {
                int val = opt.val<int>("master_mumps_icntl_" + to_string(i + 1));
                if(val != std::numeric_limits<int>::lowest())
                    _id->icntl[i] = val;
            }
            _id->job = 4;
            if(opt.val<char>("verbosity", 0) < 3)
                _id->icntl[2] = 0;
            MUMPS_STRUC_C<K>::mumps_c(_id);
            if(DMatrix::_rank == 0 && _id->infog[0] != 0)
                std::cerr << "BUG MUMPS, INFOG(1) = " << _id->infog[0] << std::endl;
            _id->icntl[2] = 0;
            delete [] I;
        }
        /* Function: solve
         *
         *  Solves the system in-place.
         *
         * Template Parameter:
         *    D              - Distribution of right-hand sides and solution vectors.
         *
         * Parameters:
         *    rhs            - Input right-hand sides, solution vectors are stored in-place.
         *    n              - Number of right-hand sides. */
        template<DMatrix::Distribution D>
        void solve(K* rhs, const unsigned short& n) {
            _id->nrhs = n;
            if(D == DMatrix::DISTRIBUTED_SOL) {
                _id->icntl[20] = 1;
                int info = _id->info[22];
                int* isol_loc = new int[info];
                K* sol_loc = new K[n * info];
                _id->sol_loc = reinterpret_cast<typename MUMPS_STRUC_C<K>::mumps_type*>(sol_loc);
                _id->lsol_loc = info;
                _id->isol_loc = isol_loc;
                _id->rhs = reinterpret_cast<typename MUMPS_STRUC_C<K>::mumps_type*>(rhs);
                _id->job = 3;
                MUMPS_STRUC_C<K>::mumps_c(_id);
                if(!DMatrix::_mapOwn && !DMatrix::_mapRecv) {
                    int nloc = DMatrix::_ldistribution[DMatrix::_rank];
                    DMatrix::initializeMap<0>(info, _id->isol_loc, sol_loc, rhs);
                    DMatrix::_ldistribution = new int[1];
                    *DMatrix::_ldistribution = nloc;
                }
                else
                    DMatrix::redistribute<0>(sol_loc, rhs);
                for(unsigned short nu = 1; nu < n; ++nu)
                    DMatrix::redistribute<0>(sol_loc + nu * info, rhs + nu * *DMatrix::_ldistribution);
                delete [] sol_loc;
                delete [] isol_loc;
            }
            else {
                _id->icntl[20] = 0;
                _id->rhs = reinterpret_cast<typename MUMPS_STRUC_C<K>::mumps_type*>(rhs);
                _id->job = 3;
                MUMPS_STRUC_C<K>::mumps_c(_id);
            }
        }
        void initialize() {
            DMatrix::initialize("MUMPS", { CENTRALIZED, DISTRIBUTED_SOL });
        }
};
#endif // DMUMPS

#ifdef MUMPSSUB
#undef HPDDM_CHECK_COARSEOPERATOR
#define HPDDM_CHECK_SUBDOMAIN
#include "preprocessor_check.hpp"
#define SUBDOMAIN HPDDM::MumpsSub
template<class K>
class MumpsSub {
    private:
        typename MUMPS_STRUC_C<K>::trait* _id;
        int*                               _I;
    public:
        MumpsSub() : _id(), _I() { }
        MumpsSub(const MumpsSub&) = delete;
        ~MumpsSub() {
            if(_id) {
                _id->job = -2;
                MUMPS_STRUC_C<K>::mumps_c(_id);
                delete _id;
                _id = nullptr;
            }
            delete [] _I;
        }
        static constexpr char _numbering = 'F';
        template<char N = HPDDM_NUMBERING>
        void numfact(MatrixCSR<K>* const& A, bool detection = false, K* const& schur = nullptr) {
            static_assert(N == 'C' || N == 'F', "Unknown numbering");
            const Option& opt = *Option::get();
            if(!_id) {
                _id = new typename MUMPS_STRUC_C<K>::trait();
                _id->job = -1;
                _id->par = 1;
                _id->comm_fortran = MPI_Comm_c2f(MPI_COMM_SELF);
                _id->sym = A->_sym ? 1 + (opt.val<char>("local_operators_not_spd", 0) || detection) : 0;
                MUMPS_STRUC_C<K>::mumps_c(_id);
            }
            _id->icntl[23] = detection;
            _id->cntl[2] = -1.0e-6;
            if(N == 'C')
                std::for_each(A->_ja, A->_ja + A->_nnz, [](int& i) { ++i; });
            _id->jcn = A->_ja;
            _id->a = reinterpret_cast<typename MUMPS_STRUC_C<K>::mumps_type*>(A->_a);
            int* listvar = nullptr;
            if(_id->job == -1) {
                _id->nrhs = 1;
                std::fill_n(_id->icntl, 5, 0);
                _id->n = A->_n;
                for(unsigned short i = 5; i < 40; ++i) {
                    int val = opt.val<int>("mumps_icntl_" + to_string(i + 1));
                    if(val != std::numeric_limits<int>::lowest())
                        _id->icntl[i] = val;
                }
                _id->lrhs = A->_n;
                _I = new int[A->_nnz];
                _id->nz = A->_nnz;
                for(int i = 0; i < A->_n; ++i)
                    std::fill(_I + A->_ia[i] - (N == 'F'), _I + A->_ia[i + 1] - (N == 'F'), i + 1);
                _id->irn = _I;
                if(schur) {
                    listvar = new int[static_cast<int>(std::real(schur[0]))];
                    std::iota(listvar, listvar + static_cast<int>(std::real(schur[0])), static_cast<int>(std::real(schur[1])));
                    _id->size_schur = _id->schur_lld = static_cast<int>(std::real(schur[0]));
                    _id->icntl[18] = 2;
                    _id->icntl[25] = 0;
                    _id->listvar_schur = listvar;
                    _id->nprow = _id->npcol = 1;
                    _id->mblock = _id->nblock = 100;
                    _id->schur = reinterpret_cast<typename MUMPS_STRUC_C<K>::mumps_type*>(schur);
                }
                _id->job = 4;
            }
            else
                _id->job = 2;
            MUMPS_STRUC_C<K>::mumps_c(_id);
            delete [] listvar;
            if(_id->infog[0] != 0)
                std::cerr << "BUG MUMPS, INFOG(1) = " << _id->infog[0] << std::endl;
            if(N == 'C')
                std::for_each(A->_ja, A->_ja + A->_nnz, [](int& i) { --i; });
        }
        unsigned short deficiency() const { return _id->infog[27]; }
        void solve(K* const x, const unsigned short& n = 1) const {
            _id->icntl[20] = 0;
            _id->rhs = reinterpret_cast<typename MUMPS_STRUC_C<K>::mumps_type*>(x);
            _id->nrhs = n;
            _id->job = 3;
            MUMPS_STRUC_C<K>::mumps_c(_id);
        }
        void solve(const K* const b, K* const x, const unsigned short& n = 1) const {
            std::copy_n(b, n * _id->n, x);
            solve(x, n);
        }
};
#endif // MUMPSSUB
} // HPDDM
#endif // _HPDDM_MUMPS_
