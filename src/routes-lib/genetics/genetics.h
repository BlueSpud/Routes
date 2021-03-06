//
// Created by Logan on 10/12/17.
//

#ifndef ROUTES_GENETICS_H
#define ROUTES_GENETICS_H

#include "population.h"

#include <pqxx/pqxx>
#include "../database/database.h"

/** A simple class to manage the genetic cycle of a population */
class Genetics {

    public:

        /**
         * This function runs the genetic algorithm to compute the most efficient path from start to dest.
         *
         * @param pop
         * The population of individuals that represent solutions to the path. This should be a new population
         * but an old one can be reused assuming that it has been simulated with the same start and dest.
         *
         * @param pod
         * The information about the hyperloop pod. Determines how curved the track can be.
         *
         * @param useDb
         * true if the database is being used
         *
         * @return
         * The points of the calculated path in meters. This will need to be converted to latitude and longitude
         * to be properly displayed.
         */
        static std::vector<glm::vec3> solve(Population& pop, Pod& pod, int generations, const glm::dvec2& start, const glm::dvec2& dest, bool useDb);

        /**
         * This function returns the route id of this route for querying the database
         *
         * @return
         * The primary key of the route table
         */
        static int getRouteId();

        /**
         * Gets the evaluated points
         *
         * @return
         * returns the evaluated points
         */
        static std::string getEval();

    private:

        /**
         * The control points of the optimal solution at the current generation
         */
        static int _id;

        /**
         * The starting latitude
         */
        static double _lat_start;

        /**
         * The ending latitude
         */
        static double _lat_end;

        /**
         * The starting longitude
         */
        static double _long_start;

        /**
         * The ending longitude
         */
        static double _long_end;

        /**
         * The evaluated points
         */
        static std::string _eval;

};

#endif //ROUTES_GENETICS_H
