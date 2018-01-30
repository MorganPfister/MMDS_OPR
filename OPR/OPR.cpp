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

using namespace tinyxml2;
using namespace std;

#ifndef XMLCheckResult
	#define XMLCheckResult(a_eResult) if (a_eResult != XML_SUCCESS) { cerr << "Error " << a_eResult << ", could not load file." << endl; getchar(); return a_eResult; }
#endif

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
		//minPathOne.push_back(getMinPathBetweenRooms(MPRoot, MARoom, StartRoom));
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
vector<pair<vector<int>, int>> generateInitialSolution(vector<vector<int>> eligibility, vector<vector<int>> minPath, vector <int> startRoomMinPath) {
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
							cumulativePath = startRoomMinPath[MAIndex];
						}
						else {
							cumulativePath += minPath[MAIndex][lastMA];
						}
						if (cumulativePath < minCumulativePath || minCumulativePath == -1) {
							minCumulativePath = cumulativePath;
							chosenOP = l;
						}
					}
				}
				solution[chosenOP].first.push_back(MAIndex);
				solution[chosenOP].second = solution[chosenOP].second + minCumulativePath;
			}
		}
	}

	return solution;
}

void improveSolution(vector<pair<vector<int>, int>> solution) {
	// Intra-route heuristic
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
		cout << "\t" << solution[i].second << endl;
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
	
	std::cout << "Computing minimum path matrix ... ";

	// Build the minimum path matrix between Manual Actions
	vector<vector<int>> minPath = computeMinimumPath(MARoot, MPRoot);

	// Build the minimum path matrix between Starting room and Manual Actions
	vector<int> startRoomMinPath = computeStartRoomMinimumPath(MARoot, MPRoot, Start);

	std::cout << "Done !" << endl;

	// Start counting time
	t1 = clock();

	// Build the eligibility matrix
	vector<vector<int>> eligibility = buildEligibility(MARoot, OPRoot);		

	// Generates the initial solution 
	vector<pair<vector<int>, int>> solution = generateInitialSolution(eligibility, minPath, startRoomMinPath);

	t2 = clock();
	time = (float)(t2 - t1) / CLOCKS_PER_SEC;
	

	printMatrix(eligibility);
	cout << endl;
	printMatrix(minPath);
	cout << endl;
	for (int i = 0; i < startRoomMinPath.size(); i++) {
		cout << startRoomMinPath[i] << " ";
	}
	cout << endl;
	printSolution(solution);
	cout << "time elapsed : " << time << endl;

	// Incrementally improves the initial solution using (meta)-heuristics
	improveSolution(solution);

	// Generates the path for each operator
	// postProcessing();

	getchar();
    return 0;
}