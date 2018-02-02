// OPR.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
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

using namespace tinyxml2;
using namespace std;

#ifndef XMLCheckResult
	#define XMLCheckResult(a_eResult) if (a_eResult != XML_SUCCESS) { cerr << "Error " << a_eResult << ", could not load file." << endl; getchar(); return a_eResult; }
#endif

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
vector<vector<int>> computeMinimumPath(XMLNode *MARoot, XMLNode *MPRoot) {
	// The matrix containing the minimum path between every Manual Action
	vector<vector<int>> minPath;
	// The vector containing the minimum path between one Manual Action and every others
	vector<int> minPathOne;

	XMLNode *MA = MARoot->FirstChild();
	XMLNode *MAloop;
	XMLElement *MARoom;
	XMLElement *MALoopRoom;

	while (MA) {
		MARoom = MA->FirstChildElement("Room");
		// Loop over every Manual Action
		MAloop = MARoot->FirstChild();
		minPathOne.clear();
		while (MAloop) {
			// If we consider the same Manual Action then the minimum path is trivially 0
			if (MA->FirstChildElement("IdAction")->GetText() == MAloop->FirstChildElement("IdAction")->GetText()) {
				minPathOne.push_back(0);
			}
			// Else, we use the min_path.xml file
			else {
				MALoopRoom = MAloop->FirstChildElement("Room");
				minPathOne.push_back(getMinPathBetweenRooms(MPRoot, MARoom, MALoopRoom));
			}
			MAloop = MAloop->NextSiblingElement();
		}
		minPath.push_back(minPathOne);
		MA = MA->NextSiblingElement();
	}

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
	srand(time(NULL));

	int numberOfOperator = eligibility[0].size();
	int startRoomIndex = -1;

	// This variable reprensents the number of possible Operator that can achieve one same Manual Operation
	int numberOfSkilledOPForMA;

	vector<int> MAMatch;
	vector<int> MAToProcess;

	int chosenOP;

	vector<pair<vector<int>, int>> solution(numberOfOperator, std::make_pair(std::vector<int> {startRoomIndex}, 0));

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

/* INTRA-ROUTE heuristic
* This heuristic looks for improvements inside the route of every operator
* The idea is to swap two consecutive Manual Action inside the route of an operator and see if this improves its path
* Then we increment the number of consecutive Manual Actions and we do this process again
*/
pair<vector<int>, int> intraRouteHeuristic(pair<vector<int>, int> route, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime) {
	bool ok = true;
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
	return route;
}

vector<pair<vector<int>, int>> improveSolution(vector<pair<vector<int>, int>> solution, vector<vector<int>> minPath, vector<int> startRoomMinPath, vector<int> serviceTime) {
	for (int i = 0; i < solution.size(); i++) {
		solution[i] = intraRouteHeuristic(solution[i], minPath, startRoomMinPath, serviceTime);
	}
	return solution;
}

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
vector<tuple<XMLElement*, XMLElement*, int>> getExtendedRouteBetweenRooms(XMLElement* Room1,XMLElement* Room2,XMLNode* MPRoot) {
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
					fichier << i + 1	<< ";" << Room1b->FirstChildElement("SrtpCode")->GetText() << Room1b->FirstChildElement("Deck")->GetText() << ";" << Room2b->FirstChildElement("SrtpCode")->GetText() << Room2b->FirstChildElement("Deck")->GetText() << ";" << travelTime << endl;
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


void printSolution(vector<pair<vector<int>, int>> solution) {
	int Cmax = 0;
	for (int i = 0; i < solution.size(); i++) {
		cout << "Operator "<< i + 1 << "\t";
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

int main(int argc, char* argv[])
{
	if (argc < 4) {
		cerr << "Usage: " << argv[0] << " <MA_file_id> <Operator_file_id> <Max_allowed_time>" << endl;
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

	XMLNode *MARoot = MAFile.FirstChild()->NextSibling();
	XMLNode *OPRoot = OPFile.FirstChild()->NextSibling();
	XMLNode *MPRoot = MinPath.FirstChild()->NextSibling();
	XMLElement *Start = StartRoom.FirstChild()->NextSiblingElement();

	cout << "Computing minimum path matrix ... ";

	// Build the minimum path matrix between Manual Actions
	vector<vector<int>> minPath = computeMinimumPath(MARoot, MPRoot);

	// Build the minimum path matrix between Starting room and Manual Actions
	vector<int> startRoomMinPath = computeStartRoomMinimumPath(MARoot, MPRoot, Start);

	cout << "Done !" << endl;

	// Start counting time
	t1 = clock();

	// Build the service time matrix
	vector<int> serviceTime = buildServiceTime(MARoot);

	// Build the eligibility matrix
	vector<vector<int>> eligibility = buildEligibility(MARoot, OPRoot);		

	// Generates the initial solution 
	vector<pair<vector<int>, int>> solution = generateInitialSolution(eligibility, minPath, startRoomMinPath, serviceTime);

	t2 = clock();
	time = (float)(t2 - t1) / CLOCKS_PER_SEC;
	
	printSolution(solution);

	// Incrementally improves the initial solution using (meta)-heuristics
	solution = improveSolution(solution, minPath, startRoomMinPath, serviceTime);

	//cout << "time elapsed : " << time << endl;

	printSolution(solution);

	// Generates the output files
	string outputFileName = "MA" + MA_file_id + "_OP" + OP_file_id + ".csv";
	postProcessing(outputFileName, solution, MPRoot, MARoot, Start, minPath, startRoomMinPath);

	getchar();
    return 0;
}