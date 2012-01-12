// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*****************************************************************************
 *   Copyright (C) 2010 by Benjamin Faigle                                   *
 *   Copyright (C) 2007-2009 by Bernd Flemisch                               *
 *   Copyright (C) 2008-2009 by Markus Wolff                                 *
 *   Institute of Hydraulic Engineering                                      *
 *   University of Stuttgart, Germany                                        *
 *   email: <givenname>.<name>@iws.uni-stuttgart.de                          *
 *                                                                           *
 *   This program is free software: you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation, either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *****************************************************************************/
#ifndef DUMUX_FVPRESSURE_HH
#define DUMUX_FVPRESSURE_HH

// dune environent:
#include <dune/istl/bvector.hh>
#include <dune/istl/operators.hh>
#include <dune/istl/solvers.hh>
#include <dune/istl/preconditioners.hh>

// dumux environment
#include "dumux/common/math.hh"
#include <dumux/decoupled/common/impetproperties.hh>

/**
 * @file
 * @brief  Finite Volume Diffusion Model
 * @author Benjamin Faigle, Bernd Flemisch, Jochen Fritz, Markus Wolff
 */

namespace Dumux
{
//! The finite volume base class for the solution of a pressure equation
/*! \ingroup multiphase
 *  TODO:
 * \tparam TypeTag The Type Tag
 */
template<class TypeTag> class FVPressure
{
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(GridView)) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(Scalar)) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(Problem)) Problem;

    typedef typename GET_PROP(TypeTag, PTAG(SolutionTypes))::ScalarSolution ScalarSolution;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(Indices)) Indices;

    typedef typename GET_PROP_TYPE(TypeTag, PTAG(CellData)) CellData;
    enum
    {
        dim = GridView::dimension, dimWorld = GridView::dimensionworld
    };
    enum
    {
        pw = Indices::pressureW,
        pn = Indices::pressureNW,
        pglobal = Indices::pressureGlobal,
        Sw = Indices::saturationW,
        Sn = Indices::saturationNW,
    };
    enum
    {
        wPhaseIdx = Indices::wPhaseIdx, nPhaseIdx = Indices::nPhaseIdx,
        wCompIdx = Indices::wPhaseIdx, nCompIdx = Indices::nPhaseIdx,
        contiWEqIdx = Indices::contiWEqIdx, contiNEqIdx = Indices::contiNEqIdx
    };

    // typedefs to abbreviate several dune classes...
    typedef typename GridView::Traits::template Codim<0>::Entity Element;
    typedef typename GridView::template Codim<0>::Iterator ElementIterator;
    typedef typename GridView::template Codim<0>::EntityPointer ElementPointer;
    typedef typename GridView::IntersectionIterator IntersectionIterator;
    typedef typename GridView::Intersection Intersection;

    // convenience shortcuts for Vectors/Matrices
    typedef Dune::FieldVector<Scalar, dimWorld> GlobalPosition;
    typedef Dune::FieldMatrix<Scalar, dim, dim> FieldMatrix;
    typedef Dune::FieldVector<Scalar, 2> PhaseVector;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(PrimaryVariables)) PrimaryVariables;

    // the typenames used for the stiffness matrix and solution vector
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(PressureCoefficientMatrix)) Matrix;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(PressureRHSVector)) RHSVector;

protected:
    //initializes the matrix to store the system of equations
    void initializeMatrix();

    //solves the system of equations to get the spatial distribution of the pressure
    void solve();

    /*! \name Access functions for protected variables  */
    //@{
    Problem& problem()
    {
        return problem_;
    }
    const Problem& problem() const
    {
        return problem_;
    }

    ScalarSolution& pressure()
    { return pressure_; }
    const ScalarSolution& pressure() const
    { return pressure_; }
    //@}
public:
    //! Public acess function for the primary variable pressure
    const Scalar pressure(int globalIdx) const
    { return pressure_[globalIdx]; }

    //function which assembles the system of equations to be solved
    void assemble(bool first);

    void getSource(Dune::FieldVector<Scalar, 2>&, const Element&, const CellData&, const bool);

    void getStorage(Dune::FieldVector<Scalar, 2>&, const Element&, const CellData&, const bool);

    void getFlux(Dune::FieldVector<Scalar, 2>&, const Intersection&, const CellData&, const bool);

    void getFluxOnBoundary(Dune::FieldVector<Scalar, 2>&,
                            const Intersection&, const CellData&, const bool);

    //pressure solution routine: update estimate for secants, assemble, solve.
    void update(bool solveTwice = true)
    {
        problem().pressureModel().assemble(false);           Dune::dinfo << "pressure calculation"<< std::endl;
        problem().pressureModel().solve();

        return;
    }

    /*! \name general methods for serialization, output */
    //@{
    // serialization methods
    //! Function needed for restart option.
    void serializeEntity(std::ostream &outstream, const Element &element)
    {
        int globalIdx = problem().variables().index(element);
        outstream << pressure_[globalIdx][0];
    }

    void deserializeEntity(std::istream &instream, const Element &element)
    {
        int globalIdx = problem().variables().index(element);
        instream >> pressure_[globalIdx][0];
    }
    //@}

    //! Constructs a FVPressure object
    /**
     * \param problem a problem class object
     */
    FVPressure(Problem& problem) :
        problem_(problem), A_(problem.gridView().size(0), problem.gridView().size(0), (2 * dim + 1)
                * problem.gridView().size(0), Matrix::random), f_(problem.gridView().size(0))
    {
        pressure_.resize(problem.gridView().size(0));
    }

protected:
    Problem& problem_;
    Matrix A_;
    RHSVector f_;
    ScalarSolution pressure_;

    std::string solverName_;
    std::string preconditionerName_;

    static constexpr int pressureType = GET_PROP_VALUE(TypeTag, PTAG(PressureFormulation)); //!< gives kind of pressure used (\f$ 0 = p_w \f$, \f$ 1 = p_n \f$, \f$ 2 = p_{global} \f$)
};

//! initializes the matrix to store the system of equations
template<class TypeTag>
void FVPressure<TypeTag>::initializeMatrix()
{
    // determine matrix row sizes
    ElementIterator eItEnd = problem().gridView().template end<0> ();
    for (ElementIterator eIt = problem().gridView().template begin<0> (); eIt != eItEnd; ++eIt)
    {
        // cell index
        int globalIdxI = problem().variables().index(*eIt);

        // initialize row size
        int rowSize = 1;

        // run through all intersections with neighbors
        IntersectionIterator isItEnd = problem().gridView().template iend(*eIt);
        for (IntersectionIterator isIt = problem().gridView().template ibegin(*eIt); isIt != isItEnd; ++isIt)
        {
            if (isIt->neighbor())
                rowSize++;
        }
        A_.setrowsize(globalIdxI, rowSize);
    }
    A_.endrowsizes();

    // determine position of matrix entries
    for (ElementIterator eIt = problem().gridView().template begin<0> (); eIt != eItEnd; ++eIt)
    {
        // cell index
        int globalIdxI = problem().variables().index(*eIt);

        // add diagonal index
        A_.addindex(globalIdxI, globalIdxI);

        // run through all intersections with neighbors
        IntersectionIterator isItEnd = problem().gridView().template iend(*eIt);
        for (IntersectionIterator isIt = problem().gridView().template ibegin(*eIt); isIt != isItEnd; ++isIt)
            if (isIt->neighbor())
            {
                // access neighbor
                ElementPointer outside = isIt->outside();
                int globalIdxJ = problem().variables().index(*outside);

                // add off diagonal index
                A_.addindex(globalIdxI, globalIdxJ);
            }
    }
    A_.endindices();

    return;
}

//! function which assembles the system of equations to be solved
/* This function assembles the Matrix and the RHS vectors to solve for
 * a pressure field with an Finite-Volume Discretization in an implicit
 * fasion. In the implementations to this base class, the methods
 * getSource(), getStorage(), getFlux() and getFluxOnBoundary() have to
 * be provided.
 * \param first Flag if pressure field is unknown
 */
template<class TypeTag>
void FVPressure<TypeTag>::assemble(bool first)
{
    // initialization: set matrix A_ to zero
    A_ = 0;
    f_ = 0;

    ElementIterator eItEnd = problem().gridView().template end<0> ();
    for (ElementIterator eIt = problem().gridView().template begin<0> (); eIt != eItEnd; ++eIt)
    {
        // cell information
        int globalIdxI = problem().variables().index(*eIt);
        CellData& cellDataI = problem().variables().cellData(globalIdxI);

        Dune::FieldVector<Scalar, 2> entries(0.);

        /*****  source term ***********/
        problem().pressureModel().getSource(entries,*eIt, cellDataI, first);
        f_[globalIdxI] = entries[1];

        /*****  flux term ***********/
        // iterate over all faces of the cell
        IntersectionIterator isItEnd = problem().gridView().template iend(*eIt);
        for (IntersectionIterator isIt = problem().gridView().template ibegin(*eIt); isIt != isItEnd; ++isIt)
        {
            /************* handle interior face *****************/
            if (isIt->neighbor())
            {
                int globalIdxJ = problem().variables().index(*(isIt->outside()));
                problem().pressureModel().getFlux(entries, *isIt, cellDataI, first);


                //set right hand side
                f_[globalIdxI] -= entries[1];
                // set diagonal entry
                A_[globalIdxI][globalIdxI] += entries[0];
                // set off-diagonal entry
                A_[globalIdxI][globalIdxJ] = -entries[0];
            }   // end neighbor


            /************* boundary face ************************/
            else
            {
                problem().pressureModel().getFluxOnBoundary(entries, *isIt, cellDataI, first);

                //set right hand side
                f_[globalIdxI] += entries[1];
                // set diagonal entry
                A_[globalIdxI][globalIdxI] += entries[0];
            }
        } //end interfaces loop
//        printmatrix(std::cout, A_, "global stiffness matrix", "row", 11, 3);

        /*****  storage term ***********/
        problem().pressureModel().getStorage(entries, *eIt, cellDataI, first);
        f_[globalIdxI] += entries[1];
        // set diagonal entry
        A_[globalIdxI][globalIdxI] += entries[0];
    } // end grid traversal
//    printmatrix(std::cout, A_, "global stiffness matrix after assempling", "row", 11,3);
//    printvector(std::cout, f_, "right hand side", "row", 10);
    return;
}

//! solves the system of equations to get the spatial distribution of the pressure
template<class TypeTag>
void FVPressure<TypeTag>::solve()
{
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(LinearSolver)) Solver;

    int verboseLevelSolver = GET_PARAM(TypeTag, int, LinearSolver, Verbosity);

    if (verboseLevelSolver)
        std::cout << __FILE__ <<": solve for pressure" << std::endl;

    Solver solver(problem());
    solver.solve(A_, pressure_, f_);
//                    printmatrix(std::cout, A_, "global stiffness matrix", "row", 11, 3);
//                    printvector(std::cout, f_, "right hand side", "row", 10, 1, 3);
//                    printvector(std::cout, pressure_, "pressure", "row", 200, 1, 3);

    return;
}

}//end namespace Dumux
#endif