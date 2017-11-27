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
void generateInitialSolution(vector<vector<int>> eligibility) {
	srand(time(NULL));

	// This variable reprensents the number of possible Operator that can achieve one same Manual Operation
	int numberOfSkilledOPForMA;
	
	// This represents the cumulative cost for each Operator. Initially it is equal to 0 for every Operator
	vector<int> workLoad(eligibility[0].size(),0);

	// This vector represents the initial solution. It will be incrementally filled
	// The first int represents the assigned Operator and the second one represents the path used by the Operator
	// Once an Operator is assigned, only the path can be changed 
	vector<int, int> initialSolution(eligibility.size());

	vector<int> MAMatch;

	for (numberOfSkilledOPForMA = 1; numberOfSkilledOPForMA < eligibility[0].size(); numberOfSkilledOPForMA++) {
		MAMatch.clear();
		for (unsigned int i = 0; i < eligibility.size(); i++) {
			if (accumulate(eligibility[i].begin(), eligibility[i].end(), 0) == numberOfSkilledOPForMA) {
				MAMatch.push_back(i);
			}
		}
		for (unsigned int k = 0; k < MAMatch.size(); k++) {
			int MAMatchRansomIndex = rand() % MAMatch.size();
			vector<int> MAToProcess = eligibility[MAMatch[MAMatchRansomIndex]];
			MAMatch.erase(MAMatch.begin() + MAMatchRansomIndex);
		}
	}
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

	XMLNode *MARoot = MAFile.FirstChild()->NextSibling();
	XMLNode *OPRoot = OPFile.FirstChild()->NextSibling();
	XMLNode *MPRoot = MinPath.FirstChild()->NextSibling();

	// Build the minimum path matrix
	vector<vector<int>> minPath = computeMinimumPath(MARoot, MPRoot);

	// Build the eligibility matrix
	vector<vector<int>> eligibility = buildEligibility(MARoot, OPRoot);	

	generateInitialSolution(eligibility);

	getchar();
    return 0;
}