#ifndef NETWORK_MONITOR_TRANSPORT_NETWORK_H
#define NETWORK_MONITOR_TRANSPORT_NETWORK_H

#include <boost/date_time/posix_time/posix_time.hpp>

#include <nlohmann/json.hpp>

#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace NetworkMonitor {

    /*! \brief A station, line, or route ID.
     */
    using Id = std::string;

    /*! \brief Network station
     *
     *  A Station struct is well formed if:
     *  - `id` is unique across all stations in the network.
     */
    struct Station {
        Id id{};
        std::string name{};

        /*! \brief Station comparison
         *
         *  Two stations are "equal" if they have the same ID.
         */
        bool operator==(const Station& other) const;
    };

    /*! \brief Network route
     *
     *  Each underground line has one or more routes. A route represents a single
     *  possible journey across a set of stops in a specified direction.
     *
     *  There may or may not be a corresponding route in the opposite direction of
     *  travel.
     *
     *  A Route struct is well formed if:
     *  - `id` is unique across all lines and their routes in the network.
     *  - The `lineId` line exists and has this route among its routes.
     *  - `stops` has at least 2 stops.
     *  - `startStationId` is the first stop in `stops`.
     *  - `endStationId` is the last stop in `stops`.
     *  - Every `stationId` station in `stops` exists.
     *  - Every stop in `stops` appears only once.
     */
    struct Route {
        Id id{};
        std::string direction{};
        Id lineId{};
        Id startStationId{};
        Id endStationId{};
        std::vector<Id> stops{};

        /*! \brief Route comparison
         *
         *  Two routes are "equal" if they have the same ID.
         */
        bool operator==(const Route& other) const;
    };

    /*! \brief Network line
     *
     *  A line is a collection of routes serving multiple stations.
     *
     *  A Line struct is well formed if:
     *  - `id` is unique across all lines in the network.
     *  - `routes` has at least 1 route.
     *  - Every route in `routes` is well formed.
     *  - Every route in `routes` has a `lineId` that is equal to this line `id`.
     */
    struct Line {
        Id id{};
        std::string name{};
        std::vector<Route> routes{};

        /*! \brief Line comparison
         *
         *  Two lines are "equal" if they have the same ID.
         */
        bool operator==(const Line& other) const;
    };

    /*! \brief Passenger event
     */
    struct PassengerEvent {
        enum class Type {
            In,
            Out
        };

        Id stationId{};
        Type type{ Type::In };
        boost::posix_time::ptime timestamp{};
    };

    void from_json(
        const nlohmann::json& src,
        PassengerEvent& dst
    );

    /*! \brief Travel plan between two stations.
     *
     *  If startStationId and endStationId are the same station, the travel steps
     *  vector contains one item.
     *
     *  If there is no valid travel route between startStationId and endStationId,
     *  or if any of startStationId and endStationId is not in the network, the
     *  travel steps vector is empty.
     */
    struct TravelRoute {
        struct Step {
            Id startStationId{};
            Id endStationId{};
            Id lineId{};
            Id routeId{};
            unsigned int travelTime{ 0 };

            bool operator==(const Step& other) const;
        };

        Id startStationId{};
        Id endStationId{};
        unsigned int totalTravelTime{ 0 };
        std::vector<Step> steps{};

        bool operator==(const TravelRoute& other) const;
    };

    /*! \brief Print operator for the `TravelRoute` class.
     */
    std::ostream& operator<<(std::ostream& os, const TravelRoute& r);

    /* \brief Serialize TravelRoute::Step to JSON.
     */
    void to_json(
        nlohmann::json& dst,
        const TravelRoute::Step& src
    );

    /* \brief Serialize TravelRoute::Step from JSON.
     */
    void from_json(
        const nlohmann::json& src,
        TravelRoute::Step& dst
    );

    /* \brief Serialize TravelRoute to JSON.
     */
    void to_json(
        nlohmann::json& dst,
        const TravelRoute& src
    );

    /* \brief Serialize TravelRoute from JSON.
     */
    void from_json(
        const nlohmann::json& src,
        TravelRoute& dst
    );

    /*! \brief Underground network representation
     */
    class TransportNetwork {
    public:
        /*! \brief Default constructor
         */
        TransportNetwork();

        /*! \brief Destructor
         */
        ~TransportNetwork();

        /*! \brief Copy constructor
         */
        TransportNetwork(
            const TransportNetwork& copied
        );

        /*! \brief Move constructor
         */
        TransportNetwork(
            TransportNetwork&& moved
        );

        /*! \brief Copy assignment operator
         */
        TransportNetwork& operator=(
            const TransportNetwork& copied
            );

        /*! \brief Move assignment operator
         */
        TransportNetwork& operator=(
            TransportNetwork&& moved
            );

        /*! \brief Populate the network from a JSON object.
         *
         *  \param src Ownership of the source JSON object is moved to this method.
         *
         *  \returns false if stations and lines where parsed successfully, but not
         *           the travel times.
         *
         *  \throws std::runtime_error This method throws if the JSON items were
         *                             parsed correctly but there was an issue
         *                             adding new stations or lines to the network.
         *  \throws nlohmann::json::exception If there was a problem parsing the
         *                                    JSON object.
         */
        bool FromJson(
            nlohmann::json&& src
        );

        /*! \brief Add a station to the network.
         *
         *  \returns false if there was an error while adding the station to the
         *           network.
         *
         *  This function assumes that the Station object is well-formed.
         *
         *  The station cannot already be in the network.
         */
        bool AddStation(
            const Station& station
        );

        /*! \brief Add a line to the network.
         *
         *  \returns false if there was an error while adding the station to the
         *           network.
         *
         *  This function assumes that the Line object is well-formed.
         *
         *  All stations served by this line must already be in the network. The
         *  line cannot already be in the network.
         */
        bool AddLine(
            const Line& line
        );

        /*! \brief Record a passenger event at a station.
         *
         *  \returns false if the station is not in the network or if the passenger
         *           event is not reconized.
         */
        bool RecordPassengerEvent(
            const PassengerEvent& event
        );

        /*! \brief Get the number of passengers currently recorded at a station.
         *
         *  The returned number can be negative: This happens if we start recording
         *  in the middle of the day and we record more exiting than entering
         *  passengers.
         *
         *  \throws std::runtime_error if the station is not in the network.
         */
        long long int GetPassengerCount(
            const Id& station
        ) const;

        /*! \brief Get list of routes serving a given station.
         *
         *  \returns An empty vector if there was an error getting the list of
         *           routes serving the station, or if the station has legitimately
         *           no routes serving it.
         *
         *  The station must already be in the network.
         */
        std::vector<Id> GetRoutesServingStation(
            const Id& station
        ) const;

        /*! \brief Set the travel time between 2 adjacent stations.
         *
         *  \returns false if there was an error while setting the travel time
         *           between the two stations.
         *
         *  The travel time is the same for all routes connecting the two stations
         *  directly.
         *
         *  The two stations must be adjacent in at least one line route. The two
         *  stations must already be in the network.
         */
        bool SetTravelTime(
            const Id& stationA,
            const Id& stationB,
            const unsigned int travelTime
        );

        /*! \brief Get the travel time between 2 adjacent stations.
         *
         *  \returns 0 if the function could not find the travel time between the
         *           two stations, or if station A and B are the same station.
         *
         *  The travel time is the same for all routes connecting the two stations
         *  directly.
         *
         *  The two stations must be adjacent in at least one line route. The two
         *  stations must already be in the network.
         */
        unsigned int GetTravelTime(
            const Id& stationA,
            const Id& stationB
        ) const;

        /*! \brief Get the total travel time between any 2 stations, on a specific
         *         route.
         *
         *  The total travel time is the cumulative sum of the travel times between
         *  all stations between `stationA` and `stationB`.
         *
         *  \returns 0 if the function could not find the travel time between the
         *           two stations, or if station A and B are the same station.
         *
         *  The two stations must be both served by the `route`. The two stations
         *  must already be in the network.
         */
        unsigned int GetTravelTime(
            const Id& line,
            const Id& route,
            const Id& stationA,
            const Id& stationB
        ) const;

        /*! \brief Get the fastest travel route from station A to station B.
         */
        TravelRoute GetFastestTravelRoute(
            const Id& stationA,
            const Id& stationB
        ) const;

        /*! \brief Get a quiet travel route alternative to the fastest route, from
         *         station A to station B.
         *
         *  \param maxSlowdownPc    Maximum travel time increase when picking a
         *                          quiet route.
         *  \param minQuietnessPc   Minimum decrease in route crowding that makes a
         *                          quiet route worth the travel time increase.
         *  \param maxNPaths        Maximum number of paths to explore. If set,
         *                          this method may yield suboptimal results.
         */
        TravelRoute GetQuietTravelRoute(
            const Id& stationA,
            const Id& stationB,
            const double maxSlowdownPc,
            const double minQuietnessPc,
            const size_t maxNPaths = std::numeric_limits<size_t>::max()
        ) const;

    private:
        // Forward-declare all internal structs.
        struct GraphNode;
        struct GraphEdge;
        struct RouteInternal;
        struct LineInternal;

        // Graph node
        // We use this as the internal station representation.
        struct GraphNode {
            Id id{};
            std::string name{};
            long long int passengerCount{ 0 };
            std::vector<std::shared_ptr<GraphEdge>> edges{};

            // Find the edge for a specific line route.
            std::vector<
                std::shared_ptr<GraphEdge>
            >::const_iterator FindEdgeForRoute(
                const std::shared_ptr<RouteInternal>& route
            ) const;
        };

        // Graph edge
        // We keep one edge for each route going through a node, even if multiple
        // routes go through the same node.
        struct GraphEdge {
            std::shared_ptr<RouteInternal> route{ nullptr };
            std::shared_ptr<GraphNode> nextStop{ nullptr };
            unsigned int travelTime{ 0 };
        };

        // Internal route representation
        struct RouteInternal {
            Id id{};
            std::shared_ptr<LineInternal> line{ nullptr };
            std::vector<std::shared_ptr<GraphNode>> stops{};
        };

        // Internal line representation
        // We map line routes by their ID.
        struct LineInternal {
            Id id{};
            std::string name{};
            std::unordered_map<Id, std::shared_ptr<RouteInternal>> routes{};
        };

        // A PathStop object represents a stop and the network edge to get to it.
        // We use it internally in our path-finding algorithms.
        struct PathStop {
            std::shared_ptr<GraphNode> node{ nullptr };
            std::shared_ptr<GraphEdge> edge{ nullptr };

            bool operator==(
                const PathStop& other
                ) const;
        };

        // We need a custom PathStop hasher to use PathStop in std::unordered_map.
        struct PathStopHash {
            size_t operator()(
                const PathStop& stop
                ) const;
        };

        // We use PathStopDist in our path-finding algorithm to rank path stops
        // by their distance from the path starting point.
        using PathStopDist = std::pair<PathStop, unsigned int>;

        // We need a custom PathStopDist comparator object to use PathStopDist in
        // std::priority_queue.
        struct PathStopDistCmp {
            bool operator()(
                const PathStopDist& a,
                const PathStopDist& b
                ) const;
        };

        using Path = std::vector<PathStopDist>;

        struct PathCmp {
            bool operator()(
                const Path& a,
                const Path& b
                ) const;
        };

        // Map station and lines by ID. We do not map line routes here, as they
        // are mapped within each line representation.
        std::unordered_map<Id, std::shared_ptr<GraphNode>> stations_{};
        std::unordered_map<Id, std::shared_ptr<LineInternal>> lines_{};

        // Get station by ID.
        std::shared_ptr<GraphNode> GetStation(
            const Id& stationId
        ) const;

        // Get line by ID.
        std::shared_ptr<LineInternal> GetLine(
            const Id& lineId
        ) const;

        // Get route by ID.
        std::shared_ptr<RouteInternal> GetRoute(
            const Id& lineId,
            const Id& routeId
        ) const;

        // This function adds a route to the internal line representation.
        bool AddRouteToLine(
            const Route& route,
            const std::shared_ptr<LineInternal>& lineInternal
        );

        // Internal version of GetFastestTravelRoute.
        // We pass station A as a PathStopDist instance instead of as a GraphNode
        // pointer to allow for warm starts, i.e. paths that start with a pre-set
        // distance-from-origin and incoming route.
        // We also pass a set of excluded stops in case we want to skip some
        // stations from the paht-finding algorithm.
        Path GetFastestTravelRoute(
            const PathStopDist& stopA,
            const std::shared_ptr<GraphNode>& stationB,
            const std::unordered_set<PathStop, PathStopHash>& excludedStops = {}
        ) const;

        // Internal function to get all the paths (up to maxNPaths) that meet a
        // certain travel time criterion:
        // bestTravelTime <= travelTime <= bestTravelTime * (1 + maxSlowdownPc)
        std::vector<Path> GetFastestTravelRoutes(
            const std::shared_ptr<TransportNetwork::GraphNode>& stationA,
            const std::shared_ptr<TransportNetwork::GraphNode>& stationB,
            const double maxSlowdownPc,
            const size_t maxNPaths = std::numeric_limits<size_t>::max()
        ) const;

        // Get the total crowding over a given path.
        unsigned int GetPathCrowding(
            const Path& path
        ) const;
    };

} // namespace NetworkMonitor

#endif // NETWORK_MONITOR_TRANSPORT_NETWORK_H