// OPR.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "OPR.h"

int main(int argc, char* argv[])
{
	srand(time(NULL));

	if (argc < 4) {
		cerr << "Usage: " << argv[0] << " <MA_file_id> <Operator_file_id> <Max_allowed_time> <Use_already_computed_min_path_matrix>" << endl;
		getchar();
		return 1;
	}

	/* Load needed files */
	string MA_file_id = argv[1];
	string MA_file_name = "Benchmark/ma_" + MA_file_id + ".xml";
	string OP_file_id = argv[2];
	string OP_file_name = "Benchmark/op_" + OP_file_id + ".xml";
	string MinPath_file_name = "MinimumPaths/paths_" + MA_file_id + ".xml";
	string Start_file_name = "Benchmark/start_0.xml";
	string MinPathMatrix_file_name = "MinimumPaths/minPathMatrix_" + MA_file_id + ".csv";

	XMLError eResult;
	XMLDocument MAFile;
	XMLDocument OPFile;
	XMLDocument MinPath;
	XMLDocument StartRoom;

	eResult = MAFile.LoadFile(MA_file_name.c_str());
	XMLCheckResult(eResult);
	eResult = OPFile.LoadFile(OP_file_name.c_str());
	XMLCheckResult(eResult);
	eResult = MinPath.LoadFile(MinPath_file_name.c_str());
	XMLCheckResult(eResult);
	eResult = StartRoom.LoadFile(Start_file_name.c_str());
	XMLCheckResult(eResult);
	/***********************/

	/* Time Calculation */
	float time;
	clock_t t1, t2;
	/********************/

	int maxTimeAllowed = stoi(argv[3]);

	XMLNode *MARoot = MAFile.FirstChild()->NextSibling();
	XMLNode *OPRoot = OPFile.FirstChild()->NextSibling();
	XMLNode *MPRoot = MinPath.FirstChild()->NextSibling();
	XMLElement *Start = StartRoom.FirstChild()->NextSiblingElement();

	ofstream file(MinPathMatrix_file_name, ios::in);
	vector<vector<int>> minPath;

	// Build the minimum path matrix between Manual Actions
	if (stoi(argv[4]) == 1 && file) { 
		file.close();
		cout << "Using MinPathMatrix" << endl;
		minPath = loadMinimumPath(MinPathMatrix_file_name);
	}
	else {
		file.close();
		cout << "Computing minimum path matrix ..." << endl << "(This may take a while, you can use the minPathMatrix_XXX.csv instead) ..." << endl;
		minPath = computeMinimumPath(MARoot, MPRoot, MinPathMatrix_file_name, stoi(MA_file_id));
	}
	// Build the minimum path matrix between Starting room and Manual Actions
	vector<int> startRoomMinPath = computeStartRoomMinimumPath(MARoot, MPRoot, Start);

	cout << "Done !" << endl;

	// Build the service time matrix
	vector<int> serviceTime = buildServiceTime(MARoot);

	// Build the eligibility matrix
	vector<vector<int>> eligibility = buildEligibility(MARoot, OPRoot);	

	// Generate the output.csv
	// Produce (in append) a file output.csv in which each row contains (separated by
	// semicolons) the data for each run executed, i.e., the MA file identifier, the Operator file
	// identifier, the CPU time needed to find the solution (seconds) and the Makespan. 

	ofstream fichier("output.csv", ios::out | ios::app);
	
	vector<int> results;
	vector<pair<vector<int>, int>> solution;
	vector<pair<vector<int>, int>> bestSolution;

	for (int i = 0; i < 5; i++) {
		// Start counting time
		t1 = clock();

		// Generates the initial solution 
		solution = generateInitialSolution(eligibility, minPath, startRoomMinPath, serviceTime);

		// Incrementally improves the initial solution using (meta)-heuristics
		solution = tabuSearch(solution, minPath, startRoomMinPath, serviceTime, eligibility, t1, maxTimeAllowed);
		// solution = customSearch(solution, minPath, startRoomMinPath, serviceTime, eligibility, t1, maxTimeAllowed);
		// solution = simulatedAnnealing(solution, minPath, startRoomMinPath, serviceTime, eligibility, t1, maxTimeAllowed);

		t2 = clock();
		time = (float)(t2 - t1) / CLOCKS_PER_SEC;
		cout << "Time for this solution = " << time << "\n";
		results.push_back(solution[getMaxCostRoute(solution)].second);
		
		//we check if the solution we just found is the best, if so we update the bestSolution
		if (i == 0) {
			bestSolution = solution;
		}
		else
		{
			if ( (solution[getMaxCostRoute(solution)].second) < (bestSolution[getMaxCostRoute(bestSolution)].second) )
			{
				bestSolution = solution;
			}
		}
		//we add the current run to the output.csv file
		if (fichier) {
			fichier << MA_file_id << ";" << OP_file_id << ";" << time << ";" << solution[getMaxCostRoute(solution)].second << "\n";
		}
		else {
			cerr << "Error opening " << "output.csv" << " file." << endl;
		}
		

		printSolution(solution);
	}


	cout << "Tabu Search : Mean_Cmax = " << accumulate(results.begin(), results.end(), 0) / results.size() << " Min_Cmax = " << *min_element(results.begin(), results.end()) << endl;

	// Generates the output files (we use the best solution found)
	string outputFileName = "MA" + MA_file_id + "_OP" + OP_file_id + ".csv";
	postProcessing(outputFileName, bestSolution, MPRoot, MARoot, Start, minPath, startRoomMinPath);

	

	// Here we finish the generation of the output.csv file
	if (fichier) {
		fichier.close();
	}
	else {
		cerr << "Error opening " << "output.csv" << " file." << endl;
	}
	fichier.close();

	getchar();
    return 0;
}


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
vector<pair<vector<int>, int>> generateInitialSolution(vector<vector<int>> eligibility, vector<vector<int>> minPath, vector <int> startRoomMinPath, vector<int> serviceTime) {
	int numberOfOperator = eligibility[0].size();
	int startRoomIndex = -1;

	// This variable reprensents the number of possible Operator that can achieve one same Manual Operation
	int numberOfSkilledOPForMA;

	vector<int> MAMatch;
	vector<int> MAToProcess;

	int chosenOP;

	vector<pair<vector<int>, int>> solution(numberOfOperator, make_pair(vector<int> {startRoomIndex}, 0));

	// Loop over the number of Operators that are able to perform the same Manual Action (from 1 to Number of Operator)
	for (numberOfSkilledOPForMA = 1; numberOfSkilledOPForMA <= numberOfOperator; numberOfSkilledOPForMA++) {
		MAMatch.clear();
		// Look for every Manual Action that have exactly numberOfSkilledOPForMA Operators that can perform it
		for (unsigned int i = 0; i < eligibility.size(); i++) {
			if (accumulate(eligibility[i].begin(), eligibility[i].end(), 0) == numberOfSkilledOPForMA) {
				MAMatch.push_back(i);
			}
		}
		if (MAMatch.size() > 0) {
			// Randomly loop over the matched Manual Action
			unsigned int MAMatchSize = MAMatch.size();
			for (unsigned int k = 0; k < MAMatchSize; k++) {
				int minCumulativePath = -1;
				int MAMatchRandomIndex = rand() % MAMatch.size();
				int MAIndex = MAMatch[MAMatchRandomIndex];
				MAToProcess = eligibility[MAIndex];
				MAMatch.erase(MAMatch.begin() + MAMatchRandomIndex);
				// Loop over the Operators to assign the Manual Action
				for (unsigned int l = 0; l < MAToProcess.size(); l++) {
					// If the Operator is skilled for this Manual Action
					if (MAToProcess[l] == 1) {
						int lastMA = solution[l].first.back();
						int cumulativePath = solution[l].second;
						if (lastMA == -1) {
							cumulativePath += startRoomMinPath[MAIndex];
						}
						else {
							cumulativePath += minPath[lastMA][MAIndex];
						}
						if (cumulativePath < minCumulativePath || minCumulativePath == -1) {
							minCumulativePath = cumulativePath;
							chosenOP = l;
						}
					}
				}
				solution[chosenOP].first.push_back(MAIndex);
				solution[chosenOP].second = minCumulativePath + serviceTime[MAIndex];
			}
		}
	}

	return solution;
}

/*
* INTRA-ROUTE heuristic
* This heuristic looks for improvements inside the route of every operator
* The idea is to swap two consecutive Manual Action inside the route of an operator and see if this improves its path
* Then we increment the number of consecutive Manual Actions and we do this process again
*/
pair<vector<int>, int> intraRouteHeuristic(pair<vector<int>, int> route, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime) {
	vector<int> length;
	pair<vector<int>, int> tempRoute = route;
	int swapMAIndex1, swapMAIndex2, it = 0;

	if (route.first.size() > 1) {
		while (it < MAX_IT_INTRA) {
			swapMAIndex1 = (rand() % (route.first.size() - 1)) + 1;
			swapMAIndex2 = (rand() % (route.first.size() - 1)) + 1;
			while (swapMAIndex2 == swapMAIndex1) {
				swapMAIndex2 = (rand() % (route.first.size() - 1)) + 1;
			}
			iter_swap(tempRoute.first.begin() + swapMAIndex1, tempRoute.first.begin() + swapMAIndex2);
			tempRoute.second = calculateLengthOfRoute(tempRoute.first, minPath, startRoomMinPath, serviceTime);
			if (tempRoute.second < route.second) {
				route = tempRoute;
				it = 0;
			}
			else {
				it++;
			}
		}
	}
	return route;
	/*bool ok = true;
	int consecutiveMA = 2;

	while (ok) {
		for (int j = 1; j + consecutiveMA - 1 < route.first.size(); j++) {
			reverse(route.first.begin() + j, route.first.begin() + (j + consecutiveMA));
			int newLength = calculateLengthOfRoute(route.first, minPath, startRoomMinPath, serviceTime);
			if (newLength < route.second) {
				route.second = newLength;
			}
			else {
				reverse(route.first.begin() + j, route.first.begin() + (j + consecutiveMA));
			}
		}
		consecutiveMA++;
		if (route.first.size() - 1 < consecutiveMA) {
			ok = false;
		}
	}
	return route;*/
}

/*
* TRANSFER heuristic
* The goal of this heuristic is to select a specific MA and re-affect it to an other eligible OP
* Each route are then improved using the intra-route heuristic
* As we want to reduce the maximum cost, we first apply this heuristic to the route which has the highest cost
*/
transferSolutionTuple transferHeuristic(vector<pair<vector<int>, int>> solution, vector<vector<int>> eligibility, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime) {
	int maxRouteIndex = getMaxCostRoute(solution), routeToInsertIndex, manualAction, MAIndex;
	pair<vector<int>, int> maxCostRoute = solution[maxRouteIndex], routeToInsert;
	pair<int, int> previousCosts;
	tuple<pair<vector<int>, int>, pair<vector<int>, int>, int> bestImprovement = make_tuple(maxCostRoute, maxCostRoute, 0);
	vector<int> routeCandidates;

	previousCosts.first = maxCostRoute.second;
	MAIndex = (rand() % (maxCostRoute.first.size() - 1)) + 1;
	manualAction = maxCostRoute.first[MAIndex];
	maxCostRoute.first.erase(maxCostRoute.first.begin() + MAIndex);

maxCostRoute.second = calculateLengthOfRoute(maxCostRoute.first, minPath, startRoomMinPath, serviceTime);
maxCostRoute = intraRouteHeuristic(maxCostRoute, minPath, startRoomMinPath, serviceTime);

routeCandidates.clear();

for (int j = 0; j < eligibility[manualAction].size(); j++) {
	if (j != maxRouteIndex && eligibility[manualAction][j] == 1) {
		routeCandidates.push_back(j);
	}
}
routeToInsertIndex = routeCandidates[rand() % routeCandidates.size()];
routeToInsert = solution[routeToInsertIndex];
previousCosts.second = routeToInsert.second;

routeToInsert.first.push_back(manualAction);
routeToInsert.second = calculateLengthOfRoute(routeToInsert.first, minPath, startRoomMinPath, serviceTime);
routeToInsert = intraRouteHeuristic(routeToInsert, minPath, startRoomMinPath, serviceTime);

solution[maxRouteIndex] = maxCostRoute;
solution[routeToInsertIndex] = routeToInsert;

return make_tuple(solution, make_pair(manualAction, maxRouteIndex), solution[getMaxCostRoute(solution)].second);
}

/*
* SWAP heuristic
* The objective of this heuristic is to swap two Manual Actions of two different routes
* The first Manual Action is one taken from the route that creates the Cmax
* The other one is chosen randomly between the Manual Action feasible by the OP and not already in his route
*/
swapSolutionTuple swapHeuristic(vector<pair<vector<int>, int>> solution, vector<vector<int>> eligibility, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime) {
	int maxRouteIndex = getMaxCostRoute(solution), swapRouteIndex;
	pair<vector<int>, int> maxCostRoute = solution[maxRouteIndex], swapRoute;
	int manualAction1 = maxCostRoute.first[(rand() % (maxCostRoute.first.size() - 1)) + 1], manualAction2;
	vector<int> MA;

	for (int i = 0; i < eligibility.size(); i++) {
		if (find(maxCostRoute.first.begin(), maxCostRoute.first.end(), i) == maxCostRoute.first.end() && eligibility[i][maxRouteIndex] == 1) {
			MA.push_back(i);
		}
	}
	manualAction2 = MA[rand() % MA.size()];

	for (int i = 0; i < solution.size(); i++) {
		if (find(solution[i].first.begin(), solution[i].first.end(), manualAction2) != solution[i].first.end()) {
			swapRoute = solution[i];
			swapRouteIndex = i;
			break;
		}
	}

	for (int i = 1; i < maxCostRoute.first.size(); i++) {
		if (maxCostRoute.first[i] == manualAction1) {
			maxCostRoute.first.erase(maxCostRoute.first.begin() + i);
			break;
		}
	}
	for (int i = 1; i < swapRoute.first.size(); i++) {
		if (swapRoute.first[i] == manualAction2) {
			swapRoute.first.erase(swapRoute.first.begin() + i);
			break;
		}
	}

	maxCostRoute.first.push_back(manualAction2);
	maxCostRoute.second = calculateLengthOfRoute(maxCostRoute.first, minPath, startRoomMinPath, serviceTime);
	swapRoute.first.push_back(manualAction1);
	swapRoute.second = calculateLengthOfRoute(swapRoute.first, minPath, startRoomMinPath, serviceTime);

	maxCostRoute = intraRouteHeuristic(maxCostRoute, minPath, startRoomMinPath, serviceTime);
	swapRoute = intraRouteHeuristic(swapRoute, minPath, startRoomMinPath, serviceTime);

	solution[maxRouteIndex] = maxCostRoute;
	solution[swapRouteIndex] = swapRoute;

	return make_tuple(solution, make_pair(make_pair(manualAction1, maxRouteIndex), make_pair(manualAction2, swapRouteIndex)), solution[getMaxCostRoute(solution)].second);
}

/*
* Calls the different heuristics to improve the initial solution
*/
vector<pair<vector<int>, int>> tabuSearch(vector<pair<vector<int>, int>> solution, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime, vector<vector<int>> eligibility, clock_t t1, int maxTimeAllowed) {
	vector<pair<vector<int>, int>> bestSolution = solution, trialSolution = solution;
	int heuristic = 0, it = 0, transferTabuListIndex = 0, swapTabuListIndex = 0;
	vector<transferSolutionTuple> transferSolutionPool;
	vector<swapSolutionTuple> swapSolutionPool;
	vector<pair<int, int>> transferTabuList;
	vector<pair<pair<int, int>, pair<int, int>>> swapTabuList;

	while ((float)(clock() - t1) / CLOCKS_PER_SEC < maxTimeAllowed) {
		cout << "time already passed : " << (float)(clock() - t1) / CLOCKS_PER_SEC << "\n";
		switch (heuristic) {
		case 0:
			it = 0;
			while (it < MAX_IT_TRANSFER) {
				transferSolutionPool.clear();
				for (int i = 0; i < POOL_SIZE_TRANSFER; i++) {
					transferSolutionPool.push_back(transferHeuristic(trialSolution, eligibility, minPath, startRoomMinPath, serviceTime));
				}
				sort(transferSolutionPool.begin(), transferSolutionPool.end(), compareTransferSolution);
				for (int i = 0; i < transferSolutionPool.size(); i++) {
					if (get<2>(transferSolutionPool[i]) < bestSolution[getMaxCostRoute(bestSolution)].second || find_if(transferTabuList.begin(), transferTabuList.end(), [=](auto e) {return e.first == get<1>(transferSolutionPool[i]).first && e.second == get<1>(transferSolutionPool[i]).second; }) == transferTabuList.end()) {
						if (transferTabuList.size() < TRANSFER_TABU_LIST_SIZE) {
							transferTabuList.push_back(get<1>(transferSolutionPool[i]));
							transferTabuListIndex++;
						}
						else {
							transferTabuListIndex = 0;
							transferTabuList[transferTabuListIndex] = get<1>(transferSolutionPool[i]);
							transferTabuListIndex++;
						}
						trialSolution = get<0>(transferSolutionPool[i]);
						if (trialSolution[getMaxCostRoute(trialSolution)].second < bestSolution[getMaxCostRoute(bestSolution)].second) {
							bestSolution = trialSolution;
							it = 0;
						}
						else {
							it++;
						}
						break;
					}
				}
			}
			heuristic = 1;
			break;

		case 1:
			it = 0;
			while (it < MAX_IT_SWAP) {
				swapSolutionPool.clear();
				for (int i = 0; i < POOL_SIZE_SWAP; i++) {
					swapSolutionPool.push_back(swapHeuristic(trialSolution, eligibility, minPath, startRoomMinPath, serviceTime));
				}
				sort(swapSolutionPool.begin(), swapSolutionPool.end(), compareSwapSolution);
				for (int i = 0; i < swapSolutionPool.size(); i++) {
					if (get<2>(swapSolutionPool[i]) < bestSolution[getMaxCostRoute(bestSolution)].second || find_if(swapTabuList.begin(), swapTabuList.end(), [=](auto e) {return e.first.first == get<1>(swapSolutionPool[i]).first.first && e.first.second == get<1>(swapSolutionPool[i]).first.second && e.second.first == get<1>(swapSolutionPool[i]).second.first && e.second.second == get<1>(swapSolutionPool[i]).second.second; }) == swapTabuList.end()) {
						if (swapTabuList.size() < SWAP_TABU_LIST_SIZE) {
							swapTabuList.push_back(get<1>(swapSolutionPool[i]));
							swapTabuListIndex++;
						}
						else {
							swapTabuListIndex = 0;
							swapTabuList[swapTabuListIndex] = get<1>(swapSolutionPool[i]);
							swapTabuListIndex++;
						}
						trialSolution = get<0>(swapSolutionPool[i]);
						if (trialSolution[getMaxCostRoute(trialSolution)].second < bestSolution[getMaxCostRoute(bestSolution)].second) {
							bestSolution = trialSolution;
							it = 0;
						}
						else {
							it++;
						}
						break;
					}
				}
			}
			heuristic = 0;
			break;
		}
	}
	return bestSolution;
}

vector<pair<vector<int>, int>> customSearch(vector<pair<vector<int>, int>> solution, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime, vector<vector<int>> eligibility, clock_t t1, int maxTimeAllowed) {
	vector<pair<vector<int>, int>> bestSolution = solution;
	int heuristic = 1, lastImprove = 0;
	
	for (int i = 0; i < solution.size(); i++) {
		bestSolution[i] = intraRouteHeuristic(solution[i], minPath, startRoomMinPath, serviceTime);
	}

	while((float)(clock() - t1) / CLOCKS_PER_SEC < maxTimeAllowed) {
		switch (heuristic) {
		case 0:
			solution = get<0>(transferHeuristic(solution, eligibility, minPath, startRoomMinPath, serviceTime));
			if (solution[getMaxCostRoute(solution)].second < bestSolution[getMaxCostRoute(bestSolution)].second) {
				lastImprove++;
				bestSolution = solution;
			}
			heuristic = rand() % 2;
			break;
		case 1:
			solution = get<0>(swapHeuristic(solution, eligibility, minPath, startRoomMinPath, serviceTime));
			if (solution[getMaxCostRoute(solution)].second < bestSolution[getMaxCostRoute(bestSolution)].second) {
				lastImprove++;
				bestSolution = solution;
			}
			heuristic = rand() % 2;
			break;
		}
		if (lastImprove > 10) {
			solution = bestSolution;
			lastImprove = 0;
		}
	}
	return bestSolution;
}

vector<pair<vector<int>, int>> simulatedAnnealing(vector<pair<vector<int>, int>> solution, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime, vector<vector<int>> eligibility, clock_t t1, int maxTimeAllowed) {
	vector<pair<vector<int>, int>> bestSolution = solution, tempSolution;
	int it = 0, heuristic = 0, currentCmax, Cmax;
	double ap, T = 0.2*bestSolution[getMaxCostRoute(bestSolution)].second;

	while ((float)(clock() - t1) / CLOCKS_PER_SEC < maxTimeAllowed) {
		it = 0;
		while (it < MAX_IT_SA) {
			switch (heuristic) {
			case 0:
				tempSolution = get<0>(transferHeuristic(solution, eligibility, minPath, startRoomMinPath, serviceTime));
				break;
			case 1:
				tempSolution = get<0>(swapHeuristic(solution, eligibility, minPath, startRoomMinPath, serviceTime));
				break;
			}
			currentCmax = bestSolution[getMaxCostRoute(bestSolution)].second;
			Cmax = tempSolution[getMaxCostRoute(tempSolution)].second;
			if (Cmax < currentCmax) {
				bestSolution = tempSolution;
				it++;
			}
			else {
				ap = exp((currentCmax - Cmax) / T);
				double ran = ((double)rand() / (RAND_MAX));
				if (ap > ran) {
					it++;
				}
			}
		}
		heuristic = heuristic == 0 ? 1 : 0;
		T = 0.75*T;
	}
	return bestSolution;
}


/*
* Custom compare for transfer heuristic 
*/
bool compareTransferSolution(transferSolutionTuple &sol1, transferSolutionTuple &sol2) {
	return get<2>(sol1) < get<2>(sol2);
}

/*
* Custom compare for swap heuristic
*/
bool compareSwapSolution(swapSolutionTuple &sol1, swapSolutionTuple &sol2) {
	return get<2>(sol1) < get<2>(sol2);
}

/*
* Gives the minimum time to go from Room1 to Room2
*/
int getMinPathBetweenRooms(XMLNode *MPRoot, XMLElement *MARoom1, XMLElement *MARoom2) {
	int travelTime = 0;

	XMLNode *MP = MPRoot->FirstChild();

	string SrtpCode1 = MARoom1->FirstChildElement("SrtpCode")->GetText();
	string Deck1 = MARoom1->FirstChildElement("Deck")->GetText();
	string SrtpCode2 = MARoom2->FirstChildElement("SrtpCode")->GetText();
	string Deck2 = MARoom2->FirstChildElement("Deck")->GetText();

	if (SrtpCode1 == SrtpCode2 && Deck1 == Deck2) {
		return travelTime;
	}

	string CmpSrtpCode1;
	string CmpDeck1;
	string CmpSrtpCode2;
	string CmpDeck2;

	while (MP) {
		CmpSrtpCode1 = MP->FirstChildElement("Room1")->FirstChildElement("SrtpCode")->GetText();
		CmpDeck1 = MP->FirstChildElement("Room1")->FirstChildElement("Deck")->GetText();
		CmpSrtpCode2 = MP->FirstChildElement("Room2")->FirstChildElement("SrtpCode")->GetText();
		CmpDeck2 = MP->FirstChildElement("Room2")->FirstChildElement("Deck")->GetText();

		if ((CmpSrtpCode1 == SrtpCode1 && CmpDeck1 == Deck1) || (CmpSrtpCode1 == SrtpCode2 && CmpDeck1 == Deck2)) {
			if ((CmpSrtpCode2 == SrtpCode1 && CmpDeck2 == Deck1) || (CmpSrtpCode2 == SrtpCode2 && CmpDeck2 == Deck2)) {
				MP->FirstChildElement("TravelTimeSeconds")->QueryIntText(&travelTime);
				return travelTime;
			}
		}

		MP = MP->NextSiblingElement();
	}
}

/*
* Builds the matrix of minimum paths between the rooms that require a Manual Action to be done
* Also, the last element of each row is the time needed between the manual action and the starting room of the operators
*/
vector<vector<int>> computeMinimumPath(XMLNode *MARoot, XMLNode *MPRoot, string fileName, int MA_file_id) {
	// The matrix containing the minimum path between every Manual Action
	vector<vector<int>> minPath;
	// The vector containing the minimum path between one Manual Action and every others
	vector<int> minPathOne;
	int length, i = 0, completion, previousCompletion = 0, totalMA;

	XMLNode *MA = MARoot->FirstChild();
	XMLNode *MAloop;
	XMLElement *MARoom;
	XMLElement *MALoopRoom;

	switch (MA_file_id) {
	case 0:
		totalMA = 459;
		break;
	case 1:
		totalMA = 100;
		break;
	case 2:
		totalMA = 200;
		break;
	case 3:
		totalMA = 350;
		break;
	}

	ofstream file(fileName, ios::out | ios::trunc);
	if (!file) {
		cout << "Error opening file " << fileName << ". Could not save the MinPathMatrix." << endl;
	}

	cout << "[";
	for (int k = 0; k < 20; k++) {
		cout << " ";
	}
	cout << "] 0%" << endl;

	while (MA) {
		MARoom = MA->FirstChildElement("Room");
		// Loop over every Manual Action
		MAloop = MARoot->FirstChild();
		minPathOne.clear();
		while (MAloop) {
			// If we consider the same Manual Action then the minimum path is trivially 0
			if (MA->FirstChildElement("IdAction")->GetText() == MAloop->FirstChildElement("IdAction")->GetText()) {
				length = 0;
			}
			// Else, we use the min_path.xml file
			else {
				MALoopRoom = MAloop->FirstChildElement("Room");
				length = getMinPathBetweenRooms(MPRoot, MARoom, MALoopRoom);
			}
			minPathOne.push_back(length);
			if (file) {
				file << length << " ";
			}
			MAloop = MAloop->NextSiblingElement();
		}
		minPath.push_back(minPathOne);
		if (file) {
			file << "\n";
		}
		MA = MA->NextSiblingElement();

		i++;
		completion = i * 100 / totalMA;
		if (completion > previousCompletion + 5) {
			previousCompletion = previousCompletion + 5;
			cout << "[";
			for (int j = 0; j < previousCompletion / 5; j++) {
				cout << "=";
			}
			for (int j = 0; j < 20 - (previousCompletion / 5); j++) {
				cout << " ";
			}
			cout << "] " << previousCompletion << "%" << endl;
		}
	}

	if (file) {
		file.close();
	}
	return minPath;
}

/*
* Loads the data in .csv files about the minPath matrix
*/
vector<vector<int>> loadMinimumPath(string fileName) {
	vector<vector<int>> minPath;
	vector<int> temp;
	ifstream file(fileName, ios::in);
	string row, length;
	int i = 0;

	if (!file) {
		cout << "Error opening file " << fileName << ". Could not save the MinPathMatrix." << endl;
	}
	else {
		while (getline(file, row)) {
			temp.clear();
			stringstream stream(row);
			while (stream >> length) {
				temp.push_back(stoi(length));
			}
			minPath.push_back(temp);
			i++;
		}
	}
	file.close();
	return minPath;
}

/*
* Returns the vector with the minimum time between the starting room and every Manual Action
*/
vector<int> computeStartRoomMinimumPath(XMLNode *MARoot, XMLNode *MPRoot, XMLElement *Start) {
	vector<int> startRoomMinPath;

	XMLNode *MA = MARoot->FirstChild();
	XMLElement *MARoom;

	while (MA) {
		MARoom = MA->FirstChildElement("Room");
		startRoomMinPath.push_back(getMinPathBetweenRooms(MPRoot, MARoom, Start));
		MA = MA->NextSiblingElement();
	}

	return startRoomMinPath;
}

/*
* Calculates the total cost of a route
*/
int calculateLengthOfRoute(vector<int> route, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime) {
	int length = 0;
	if (route.size() > 1) {
		length += startRoomMinPath[route[1]] + serviceTime[route[1]];
		for (int i = 1; i + 1 < route.size(); i++) {
			length += minPath[route[i]][route[i + 1]] + serviceTime[route[i + 1]];
		}
	}
	return length;
}

/*
* Returns the index of the operator that creates the Cmax
*/
int getMaxCostRoute(vector<pair<vector<int>, int>> solution) {
	int Cmax = 0, route;
	for (int i = 0; i < solution.size(); i++) {
		if (solution[i].second > Cmax) {
			Cmax = solution[i].second;
			route = i;
		}
	}
	return route;
}

/*
* Returns the i-th Manual Action
*/
XMLNode* getMAUsingMAIndex(int MAindex, XMLNode *MARoot) {
	XMLNode *MA = MARoot->FirstChild();

	int loop = 1;

	while (loop <= MAindex) {
		MA = MA->NextSiblingElement();
		loop++;
	}
	return MA;
}

/*
* Returns the extended paths between two rooms (eg. all the rooms inbetween)
*/
vector<tuple<XMLElement*, XMLElement*, int>> getExtendedRouteBetweenRooms(XMLElement* Room1, XMLElement* Room2, XMLNode* MPRoot) {
	vector<tuple<XMLElement*, XMLElement*, int>> extendedPath;
	int travelTime;

	XMLNode *MP = MPRoot->FirstChild();
	XMLNode* MPGraphArc;

	string SrtpCode1 = Room1->FirstChildElement("SrtpCode")->GetText();
	string Deck1 = Room1->FirstChildElement("Deck")->GetText();
	string SrtpCode2 = Room2->FirstChildElement("SrtpCode")->GetText();
	string Deck2 = Room2->FirstChildElement("Deck")->GetText();

	string CmpSrtpCode1;
	string CmpDeck1;
	string CmpSrtpCode2;
	string CmpDeck2;

	while (MP) {
		CmpSrtpCode1 = MP->FirstChildElement("Room1")->FirstChildElement("SrtpCode")->GetText();
		CmpDeck1 = MP->FirstChildElement("Room1")->FirstChildElement("Deck")->GetText();
		CmpSrtpCode2 = MP->FirstChildElement("Room2")->FirstChildElement("SrtpCode")->GetText();
		CmpDeck2 = MP->FirstChildElement("Room2")->FirstChildElement("Deck")->GetText();

		if ((CmpSrtpCode1 == SrtpCode1 && CmpDeck1 == Deck1) && (CmpSrtpCode2 == SrtpCode2 && CmpDeck2 == Deck2)) {
			MPGraphArc = MP->FirstChildElement("RoomsBetween")->FirstChildElement("GraphArc");
			while (MPGraphArc) {
				MPGraphArc->FirstChildElement("TravelTime")->QueryIntText(&travelTime);
				extendedPath.push_back(std::make_tuple(MPGraphArc->FirstChildElement("Room1"), MPGraphArc->FirstChildElement("Room2"), travelTime));
				MPGraphArc = MPGraphArc->NextSiblingElement();
			}
			break;
		}
		MP = MP->NextSiblingElement();
	}

	return extendedPath;
}

/*
* Writes in a file the info of the paths taken by the operators
*/
void postProcessing(string fileName, vector<pair<vector<int>, int>> solution, XMLNode *MPRoot, XMLNode *MARoot, XMLElement *Start, vector<vector<int>> minPath, vector<int> startRoomMinPath) {
	vector<tuple<XMLElement*, XMLElement*, int>> extendedPath;

	int travelTime, serviceTime, cumulativeTime;

	ofstream fichier(fileName, ios::out | ios::trunc);
	if (fichier) {
		fichier << "OperatorID;Room1;Room2;TravelTime" << endl;
		for (int i = 0; i < solution.size(); i++) {
			vector<int> route = solution[i].first;
			for (int j = 0; j + 1 < route.size(); j++) {
				XMLElement *Room1, *Room2;
				if (route[j] == -1) {
					Room1 = Start;
				}
				else {
					Room1 = getMAUsingMAIndex(route[j], MARoot)->FirstChildElement("Room");
				}
				Room2 = getMAUsingMAIndex(route[j + 1], MARoot)->FirstChildElement("Room");
				extendedPath = getExtendedRouteBetweenRooms(Room1, Room2, MPRoot);
				for (int k = 0; k < extendedPath.size(); k++) {
					XMLElement* Room1b = std::get<0>(extendedPath[k]);
					XMLElement* Room2b = std::get<1>(extendedPath[k]);
					travelTime = std::get<2>(extendedPath[k]);
					fichier << i + 1 << ";" << Room1b->FirstChildElement("SrtpCode")->GetText() << Room1b->FirstChildElement("Deck")->GetText() << ";" << Room2b->FirstChildElement("SrtpCode")->GetText() << Room2b->FirstChildElement("Deck")->GetText() << ";" << travelTime << endl;
				}
			}
		}
		fichier << endl;
		fichier << "OperatorID;MA;Room;ProcessingTime;TotalCumulativeTime" << endl;
		for (int i = 0; i < solution.size(); i++) {
			vector<int> route = solution[i].first;
			cumulativeTime = 0;
			if (route.size() > 1) {
				for (int j = 1; j < route.size(); j++) {
					XMLNode *MA = getMAUsingMAIndex(route[j], MARoot);
					MA->FirstChildElement("ServiceTime")->QueryIntText(&serviceTime);
					if (j == 1) {
						cumulativeTime += startRoomMinPath[route[j]] + serviceTime;
					}
					else {
						cumulativeTime += minPath[route[j - 1]][route[j]] + serviceTime;
					}
					fichier << i + 1 << ";" << MA->FirstChildElement("IdAction")->GetText() << ";" << MA->FirstChildElement("Room")->FirstChildElement("SrtpCode")->GetText() << MA->FirstChildElement("Room")->FirstChildElement("Deck")->GetText() << ";" << serviceTime << ";" << cumulativeTime << endl;
				}
			}
		}
		fichier.close();
	}
	else {
		cerr << "Error opening " << fileName << " file." << endl;
	}
	fichier.close();
}

/*
* Prints the content of a matrix
*/
void printMatrix(vector<vector<int>> matrix) {
	for (unsigned int i = 0; i < matrix.size(); i++) {
		for (unsigned int j = 0; j < matrix[i].size(); j++) {
			std::cout << matrix[i][j] << " ";
		}
		cout << endl;
	}
}

/*
* Prints the content of a route
*/
void printRoute(pair<vector<int>, int> route) {
	for (int i = 0; i < route.first.size(); i++) {
		cout << route.first[i] << " ";
	}
	cout << "\t" << route.second << endl;
}

/*
* Prints the content of a solution
*/
void printSolution(vector<pair<vector<int>, int>> solution) {
	int Cmax = 0;
	for (int i = 0; i < solution.size(); i++) {
		cout << "Operator " << i + 1 << "\t";
		for (int j = 0; j < solution[i].first.size(); j++) {
			cout << solution[i].first[j] << " ";
		}
		if (solution[i].second > Cmax) {
			Cmax = solution[i].second;
		}
		cout << "\t\t" << solution[i].second << endl;
	}
	cout << "Cmax = " << Cmax << endl;
}

/*
* Builds the service time matrix
*/
vector<int> buildServiceTime(XMLNode *MARoot) {
	vector<int> serviceTime;

	XMLNode *MA = MARoot->FirstChild();
	while (MA) {
		int time;
		MA->FirstChildElement("ServiceTime")->QueryIntText(&time);
		serviceTime.push_back(time);
		MA = MA->NextSiblingElement();
	}

	return serviceTime;
}

/*
* Builds the eligibility matrix :
*		eligibility[i][j] = 1 if Operator j has the required skill for the Manual Operation i
*		eligibility[i][j] = 0 otherwise
*/
vector<vector<int>> buildEligibility(XMLNode *MARoot, XMLNode *OPRoot) {
	// This matrix contains all the possible operators that can perform each manual operation
	vector<vector<int>> eligibility;
	// The vector that contains the eligible operator for one Manual Action
	vector<int> eligible;

	XMLNode *MA = MARoot->FirstChild();
	XMLNode *OP;

	while (MA) {
		// Reassign OP to the start of the XML file
		OP = OPRoot->FirstChild();

		// Get the skill required by the Manual Action 
		string RequiredSkill = MA->FirstChildElement("SystemCode")->GetText();

		eligible.clear();
		while (OP) {
			XMLElement *OperatorSkill = OP->FirstChildElement("SystemCodes")->FirstChildElement("string");

			bool hasSkill = false;
			while (OperatorSkill) {
				if (OperatorSkill->GetText() == RequiredSkill) {
					hasSkill = true;
					eligible.push_back(1);
					break;
				}
				OperatorSkill = OperatorSkill->NextSiblingElement();
			}
			if (!hasSkill) {
				eligible.push_back(0);
			}
			OP = OP->NextSiblingElement();
		}

		eligibility.push_back(eligible);
		MA = MA->NextSiblingElement();
	}

	return eligibility;
}