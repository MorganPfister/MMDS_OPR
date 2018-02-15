#pragma once

#include "tinyxml2.h" 
#include "tinyxml2.cpp"
#include <iostream>
#include <string>
#include <vector>
#include <iterator>
#include <numeric>
#include <time.h> 
#include <fstream>
#include <tuple>
#include <algorithm>
#include <limits.h>
#include <sstream>
#include <iterator>

using namespace tinyxml2;
using namespace std;

#ifndef XMLCheckResult
	#define XMLCheckResult(a_eResult) if (a_eResult != XML_SUCCESS) { cerr << "Error " << a_eResult << ", could not load file." << endl; getchar(); return a_eResult; }
#endif
#define POOL_SIZE_TRANSFER		10
#define POOL_SIZE_SWAP			10
#define TRANSFER_TABU_LIST_SIZE 8
#define SWAP_TABU_LIST_SIZE		8
#define MAX_IT_TRANSFER			5
#define MAX_IT_SWAP				5
#define MAX_IT_INTRA			10
#define MAX_IT_SA				5

typedef tuple<vector<pair<vector<int>, int>>, pair<int, int>, int> transferSolutionTuple;
/*
* Custom compare for transfer heuristic
*/
bool compareTransferSolution(transferSolutionTuple &sol1, transferSolutionTuple &sol2);

typedef tuple<vector<pair<vector<int>, int>>, pair<pair<int, int>, pair<int, int>>, int> swapSolutionTuple;
/*
* Custom compare for swap heuristic
*/
bool compareSwapSolution(swapSolutionTuple &sol1, swapSolutionTuple &sol2);

/*
* Prints the content of a matrix
*/
void printMatrix(vector<vector<int>> matrix);
/*
* Prints the content of a route
*/
void printRoute(pair<vector<int>, int> route);
/*
* Prints the content of a solution
*/
void printSolution(vector<pair<vector<int>, int>> solution);
/*
* Builds the service time matrix
*/
vector<int> buildServiceTime(XMLNode *MARoot);
/*
* Builds the eligibility matrix :
*		eligibility[i][j] = 1 if Operator j has the required skill for the Manual Operation i
*		eligibility[i][j] = 0 otherwise
*/
vector<vector<int>> buildEligibility(XMLNode *MARoot, XMLNode *OPRoot);
/*
* Gives the minimum time to go from Room1 to Room2
*/
int getMinPathBetweenRooms(XMLNode *MPRoot, XMLElement *MARoom1, XMLElement *MARoom2);
/*
* Builds the matrix of minimum paths between the rooms that require a Manual Action to be done
* Also, the last element of each row is the time needed between the manual action and the starting room of the operators
*/
vector<vector<int>> computeMinimumPath(XMLNode *MARoot, XMLNode *MPRoot, string fileName, int MA_file_id);
/*
 * Loads the data in .csv files about the minPath matrix
 */
vector<vector<int>> loadMinimumPath(string fileName);
/*
* Returns the vector with the minimum time between the starting room and every Manual Action
*/
vector<int> computeStartRoomMinimumPath(XMLNode *MARoot, XMLNode *MPRoot, XMLElement *Start);
/*
* Builds an initial (possibly) good and feasible solution to the problem
*		General Algorithm :
*			- Loop over k from 1 to NumberOfOperators
*				- Loop over the eligibility matrix
*					- If the number of possible Operators that are skilled for the ongoing Manual Action is equal to k
*						- Assign an operator to fulfill the Manual Action
*
*		The idea of the k loop is to assign Manual Actions that are feasible by a few number of Operator first
*			(e.g. if a Manual Action can be done only by one Operator, it has to be assigned to him anyway)
*		The assignment of an Operator to a Manual Action is randomly chosen between the eligible Operators for this
*			Manual Action, taking into account the already assigned Manual Actions
*			-> we would prefer to assign a Manual Action to an Operator who hasn't be assigned yet any Manual Action or whose route is "small"
*/
vector<pair<vector<int>, int>> generateInitialSolution(vector<vector<int>> eligibility, vector<vector<int>> minPath, vector <int> startRoomMinPath, vector<int> serviceTime);
/*
* Calculates the total cost of a route
*/
int calculateLengthOfRoute(vector<int> route, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime);
/*
* INTRA-ROUTE heuristic
* This heuristic looks for improvements inside the route of every operator
* The idea is to swap two consecutive Manual Action inside the route of an operator and see if this improves its path
* Then we increment the number of consecutive Manual Actions and we do this process again
*/
pair<vector<int>, int> intraRouteHeuristic(pair<vector<int>, int> route, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime);
/*
* Returns the index of the operator that creates the Cmax
*/
int getMaxCostRoute(vector<pair<vector<int>, int>> solution);
/*
* TRANSFER heuristic
* The goal of this heuristic is to select a specific MA and re-affect it to an other eligible OP
* Each route are then improved using the intra-route heuristic
* As we want to reduce the maximum cost, we first apply this heuristic to the route which has the highest cost
*/
transferSolutionTuple transferHeuristic(vector<pair<vector<int>, int>> solution, vector<vector<int>> eligibility, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime);
/*
* SWAP heuristic
* The objective of this heuristic is to swap two Manual Actions of two different routes
* The first Manual Action is one taken from the route that creates the Cmax
* The other one is chosen randomly between the Manual Action feasible by the OP and not already in his route
*/
swapSolutionTuple swapHeuristic(vector<pair<vector<int>, int>> solution, vector<vector<int>> eligibility, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime);
/*
* Calls the different heuristics to improve the initial solution
*/
vector<pair<vector<int>, int>> tabuSearch(vector<pair<vector<int>, int>> solution, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime, vector<vector<int>> eligibility, clock_t t1, int maxTimeAllowed);
vector<pair<vector<int>, int>> customSearch(vector<pair<vector<int>, int>> solution, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime, vector<vector<int>> eligibility, clock_t t1, int maxTimeAllowed);
vector<pair<vector<int>, int>> simulatedAnnealing(vector<pair<vector<int>, int>> solution, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime, vector<vector<int>> eligibility, clock_t t1, int maxTimeAllowed);
/*
 * Returns the i-th Manual Action
 */
XMLNode* getMAUsingMAIndex(int MAindex, XMLNode *MARoot);
/*
* Returns the extended paths between two rooms (eg. all the rooms inbetween)
*/
vector<tuple<XMLElement*, XMLElement*, int>> getExtendedRouteBetweenRooms(XMLElement* Room1, XMLElement* Room2, XMLNode* MPRoot);
/*
* Writes in a file the info of the paths taken by the operators
*/
void postProcessing(string fileName, vector<pair<vector<int>, int>> solution, XMLNode *MPRoot, XMLNode *MARoot, XMLElement *Start, vector<vector<int>> minPath, vector<int> startRoomMinPath);