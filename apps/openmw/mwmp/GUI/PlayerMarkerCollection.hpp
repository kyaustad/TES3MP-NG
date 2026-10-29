// Copied from MWGui::CustomMarkerCollection

#ifndef OPENMW_PLAYERMARKERCOLLECTION_HPP
#define OPENMW_PLAYERMARKERCOLLECTION_HPP

#include "../../mwgui/mapwindow.hpp"

namespace mwmp
{
    /*
        OpenMW 0.49+ keys markers by cell RefId; other players' markers live in a collection owned by the map
        windows, which draw them next to custom markers
    */
    using PlayerMarkerCollection = MWGui::CustomMarkerCollection;
}


#endif //OPENMW_PLAYERMARKERCOLLECTION_HPP
