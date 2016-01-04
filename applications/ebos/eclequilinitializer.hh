/*
  Copyright (C) 2014 by Andreas Lauser

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * \file
 *
 * \copydoc Ewoms::EclEquilInitializer
 */
#ifndef EWOMS_ECL_EQUIL_INITIALIZER_HH
#define EWOMS_ECL_EQUIL_INITIALIZER_HH

#include <opm/material/fluidstates/CompositionalFluidState.hpp>

// the ordering of these includes matters. do not touch it if you're not prepared to deal
// with some trouble!
#include <dune/grid/cpgrid/GridHelpers.hpp>
#include <opm/core/props/BlackoilPropertiesFromDeck.hpp>
#include <opm/core/simulator/initStateEquil.hpp>
#include <opm/core/simulator/BlackoilState.hpp>

#include <vector>

namespace Ewoms {
/*!
 * \ingroup EclBlackOilSimulator
 *
 * \brief Computes the initial condition based on the EQUIL keyword from ECL.
 *
 * So far, it uses the "initStateEquil()" function from opm-core. Since this method is
 * very much glued into the opm-core data structures, it should be reimplemented in the
 * medium to long term for some significant memory savings and less significant
 * performance improvements.
 */
template <class TypeTag>
class EclEquilInitializer
{
    typedef typename GET_PROP_TYPE(TypeTag, Simulator) Simulator;
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;
    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, MaterialLaw) MaterialLaw;

    typedef Opm::CompositionalFluidState<Scalar, FluidSystem> ScalarFluidState;

    enum { numPhases = FluidSystem::numPhases };
    enum { oilPhaseIdx = FluidSystem::oilPhaseIdx };
    enum { gasPhaseIdx = FluidSystem::gasPhaseIdx };
    enum { waterPhaseIdx = FluidSystem::waterPhaseIdx };

    enum { numComponents = FluidSystem::numComponents };
    enum { oilCompIdx = FluidSystem::oilCompIdx };
    enum { gasCompIdx = FluidSystem::gasCompIdx };
    enum { waterCompIdx = FluidSystem::waterCompIdx };

    enum { dimWorld = GridView::dimensionworld };

public:
    template <class MaterialLawManager>
    EclEquilInitializer(const Simulator& simulator,
                        std::shared_ptr<MaterialLawManager> materialLawManager)
        : simulator_(simulator)
    {
        const auto& gridManager = simulator.gridManager();
        const auto& equilGrid   = gridManager.equilGrid();

        // create the data structures which are used by initStateEquil()
        Opm::parameter::ParameterGroup tmpParam;
        Opm::BlackoilPropertiesFromDeck opmBlackoilProps(
            gridManager.deck(),
            gridManager.eclState(),
            materialLawManager,
            Opm::UgGridHelpers::numCells(equilGrid),
            Opm::UgGridHelpers::globalCell(equilGrid),
            Opm::UgGridHelpers::cartDims(equilGrid),
            tmpParam);

        const unsigned numElems = equilGrid.size(/*codim=*/0);
        assert( gridManager.grid().size(/*codim=*/0) == static_cast<int>(numElems) );
        // initialize the boiler plate of opm-core the state structure.
        Opm::BlackoilState opmBlackoilState;
        opmBlackoilState.init(numElems,
                              /*numFaces=*/0, // we don't care here
                              numPhases);

        // do the actual computation.
        Opm::initStateEquil(equilGrid,
                            opmBlackoilProps,
                            gridManager.deck(),
                            gridManager.eclState(),
                            simulator.problem().gravity()[dimWorld - 1],
                            opmBlackoilState);

        // copy the result into the array of initial fluid states
        initialFluidStates_.resize(numElems);
        for (unsigned elemIdx = 0; elemIdx < numElems; ++elemIdx) {
            auto &fluidState = initialFluidStates_[elemIdx];

            // get the PVT region index of the current element
            unsigned regionIdx = simulator_.problem().pvtRegionIndex(elemIdx);

            // set the phase saturations
            for (unsigned phaseIdx = 0; phaseIdx < numPhases; ++phaseIdx) {
                Scalar S = opmBlackoilState.saturation()[elemIdx*numPhases + phaseIdx];
                fluidState.setSaturation(phaseIdx, S);
            }

            // set the temperature
            const auto& temperatureVector = opmBlackoilState.temperature();
            Scalar T = FluidSystem::surfaceTemperature;
            if (!temperatureVector.empty())
                T = temperatureVector[elemIdx];
            fluidState.setTemperature(T);

            // set the phase pressures. the Opm::BlackoilState only provides the oil
            // phase pressure, so we need to calculate the other phases' pressures
            // ourselfs.
            Dune::FieldVector< Scalar, numPhases >  pC( 0 );
            const auto& matParams = simulator.problem().materialLawParams(elemIdx);
            MaterialLaw::capillaryPressures(pC, matParams, fluidState);
            Scalar po = opmBlackoilState.pressure()[elemIdx];
            for (unsigned phaseIdx = 0; phaseIdx < numPhases; ++phaseIdx)
                fluidState.setPressure(phaseIdx, po + (pC[phaseIdx] - pC[oilPhaseIdx]));

            // reset the phase compositions
            for (unsigned phaseIdx = 0; phaseIdx < numPhases; ++phaseIdx)
                for (unsigned compIdx = 0; compIdx < numComponents; ++compIdx)
                    fluidState.setMoleFraction(phaseIdx, compIdx, 0.0);

            // the composition of the water phase is simple: it only consists of the
            // water component.
            fluidState.setMoleFraction(waterPhaseIdx, waterCompIdx, 1.0);

            if (gridManager.deck()->hasKeyword("DISGAS")) {
                // for gas and oil we have to translate surface volumes to mole fractions
                // before we can set the composition in the fluid state
                Scalar Rs = opmBlackoilState.gasoilratio()[elemIdx];
                Scalar RsSat = FluidSystem::saturatedDissolutionFactor(fluidState, oilPhaseIdx, regionIdx);

                if (Rs > RsSat)
                    Rs = RsSat;

                // convert the Rs factor to mole fraction dissolved gas in oil
                Scalar XoG = FluidSystem::convertRsToXoG(Rs, regionIdx);
                Scalar xoG = FluidSystem::convertXoGToxoG(XoG, regionIdx);

                fluidState.setMoleFraction(oilPhaseIdx, oilCompIdx, 1 - xoG);
                fluidState.setMoleFraction(oilPhaseIdx, gasCompIdx, xoG);
            }

            // retrieve the surface volume of vaporized gas
            if (gridManager.deck()->hasKeyword("VAPOIL")) {
                Scalar Rv = opmBlackoilState.rv()[elemIdx];
                Scalar RvSat = FluidSystem::saturatedDissolutionFactor(fluidState, gasPhaseIdx, regionIdx);

                if (Rv > RvSat)
                    Rv = RvSat;

                // convert the Rs factor to mole fraction dissolved gas in oil
                Scalar XgO = FluidSystem::convertRvToXgO(Rv, regionIdx);
                Scalar xgO = FluidSystem::convertXgOToxgO(XgO, regionIdx);

                fluidState.setMoleFraction(gasPhaseIdx, oilCompIdx, xgO);
                fluidState.setMoleFraction(gasPhaseIdx, gasCompIdx, 1 - xgO);
            }
        }
    }

    /*!
     * \brief Return the initial thermodynamic state which should be used as the initial
     *        condition.
     *
     * This is supposed to correspond to hydrostatic conditions.
     */
    const ScalarFluidState& initialFluidState(unsigned elemIdx) const
    { return initialFluidStates_[elemIdx]; }

protected:
    const Simulator& simulator_;

    std::vector<ScalarFluidState> initialFluidStates_;
};
} // namespace Ewoms

#endif
