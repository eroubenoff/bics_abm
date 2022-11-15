#include <string>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <igraph.h>
#include<fstream>
#include "BICS_ABM.h"
#include <random>
#include <map>
#include <fstream>
#include <iostream>
#include <sstream>


using namespace std;


/*
 * Adds a vertex to the graph. 
 *
 * @param g graph pointer
 * @param i index of node to add 
 */
void add_vertex(igraph_t *g,
        string age,
        string gender,
        string ethnicity,
        int num_cc_nonhh, 
        int vaccine_priority,
        string hhid) {

    if (false) {
        cout << "Age: " << age << " Gender: " << gender << " Ethnicity: " << \
            ethnicity << " num_Cc_nonhh: " << num_cc_nonhh << " hhid: " << hhid << endl;
    }
    // Add vertex 
    igraph_add_vertices(g, 1, NULL);

    int i = igraph_vcount(g) - 1;

    // Age
    SETVAS(g, "age", i, age.c_str());

    // Gender
    SETVAS(g, "gender", i, gender.c_str());

    // Ethnicity
    SETVAS(g, "ethnicity", i, ethnicity.c_str());

    // Num cc nonhh
    SETVAN(g, "num_cc_nonhh", i, num_cc_nonhh);

    // Hhid
    SETVAS(g, "hhid", i, hhid.c_str());

    // Fixed characteristics
    SETVAS(g, "disease_status", i, "S");
    SETVAN(g, "remaining_days_exposed", i, -1);
    SETVAN(g, "remaining_days_sick", i, -1);
    SETVAN(g, "vaccine_status", i, 0);
    SETVAN(g, "vaccine_priority", i, vaccine_priority);
    SETVAN(g, "time_until_v2", i, -1);




}






/* 
 * Read a population from a csv
 * 
 * @param string path 
 * 	path of the already-generated population, probably from python
 * @param igraph_t &graph
 *	pointer to graph to initialize population in 		
 * 					  

 * @return none; modifies &graph in place
 */
void read_pop_from_csv(string path, igraph_t *g) {

    // Create pointer to pop file and open
    ifstream fin;
    fin.open("pop.csv");

    // Read the csv header (used to create attributes)
    vector<string> header = get_csv_row(fin);

    // Invert header into unordered map for column lookup
    unordered_map<string, int> colnames;
    for (int i = 0; i < header.size(); i++) {
        colnames[header[i]] = i;
    }

    // Loop through the attributes of the first nodes
    vector<string> row;
    int i = 0;
    while( fin.peek() != EOF ) {

        // Read the row
        row = get_csv_row(fin);

        add_vertex(g, 
                row[colnames["age"]],
                row[colnames["gender"]],
                row[colnames["ethnicity"]],
                stoi(row[colnames["num_cc_nonhh"]]),
                0,
                row[colnames["hhid"]]
                );

        i++;

    }

    cout << "Loaded " << i << " nodes from " << path << endl;


    /* Close the fstream */
    fin.close();


}



/*
 * Function to recode POLYMOD age categories to match BICS
 *
 * @param string age_s: input string
 * @return string, age in bins
 */
string recode_age(string age_s) {

    if (age_s == "NA") return "NA"; 

    int age = stoi(age_s);

    if (age < 18)
        return "[0,18)"; 
    else if (age < 25)
        return "[18,25)";
    else if (age < 35)
        return "[25,35)";
    else if (age < 45)
        return "[35,45)";
    else if (age < 55)
        return "[45,55)";
    else if (age < 65)
        return "[55,65)" ;
    else if (age < 75)
        return "[65,75)"; 
    else if (age < 85)
        return "[75,85)";
    else if (age >= 85)
        return ">85";
    else 
        throw runtime_error("Invalid age");


}







/*
 * Function to recode POLYMOD gender categories to match BICS
 *
 * @param string age_s: input string
 * @return string, gender
 */
string recode_gender(string gender) {


    if (gender == "NA")
        return "NA";
    if (gender == "Male") 
        return "Male";
    else if (gender == "Female") 
        return "Female";
    else if (gender == "M")
        return "Male"; 
    else if (gender == "F")
        return "Female";
    else if (gender == "0")
        return "Male";
    else if (gender == "1")
        return "Female";
    else 
        throw runtime_error("Invalid gender");

}


/*
 * Helper function to wrap any strings containing
 * commas in quotes
 *
 */
string escape_commas(string s) {

    bool has_comma = false;

    for (int i = 0; i < s.size(); i++) {
        if (s[i] == ',') has_comma = true;
        else continue;
    }

    if (has_comma) {
        s = "\""  + s + "\"";
    }

    return s;
    
}


/*
 * Generates vaccine priorties. Rules come in the form of:
 *
 * > tuple(string colname, string value)
 *
 * So, to have first priority go to oldest age group and second
 * go to black women of any age:
 *
 * [
 *  [("age", "[65, 100]")],
 *  [("race", "black"), ("gender" "F")] // Follows AND logic
 * ]
 *
 * Priority is descending: people get the highest priority
 * they are eligible for.
 *
 * "hesitancy" can be passed as special tuple as well. If passed,
 * this special will take bernoulli draw if the node will take vaccine
 * or not. 
 *
 *
 * The first entries are BICS and the second are POLYMOD,
 * so the full length of the vector is BICS_nrow + POLYMOD_nrow
 *
 */

vector<int> vaccine_priority(
        const Data *data, 
        const vector<vector<tuple<string, string>>> rules,
        mt19937 generator
        ){

    /* Right now this is an nrow * nconditions operation: slow*/

    int priority;
    vector<bool> satisfies_v;
    bool satisfies;
    string logic;
    string colname;
    string condition;
    vector<int> priority_vec; 

    /* Loop through each row */
    for (int i = 0; i < data->BICS_nrow; i++) {
        /* Loop through each priority level */
        priority = -1;

        for (int j = 0; j < rules.size(); j++) {
            satisfies_v.clear();
            logic = "OR";

            /* Loop through each condition */
            for (int k = 0; k < rules[j].size(); k++) {

                colname = get<0>(rules[j][k]);
                condition = get<1>(rules[j][k]);

                /* Three special conditions: hesitance, general, and logic */

                if (colname == "hesitancy") {
                    /* Random draw with passed parameter */
                    satisfies_v.push_back(bernoulli_distribution{stof(condition)}(generator));
                } 
                else if (colname == "general") {
                    satisfies_v.clear();
                    satisfies_v.push_back(true);
                    break;
                }  
                else if (colname == "logic") {
                    logic = condition;
                }

                /* Handle normal conditions */
                else if (data->BICS(i, colname) == condition) {
                    satisfies_v.push_back(true);
                } 
                else {
                    satisfies_v.push_back(false);
                }
            }


            /* Evaluate the logic */
            if (logic == "AND") {
                satisfies = true;
                for (bool i: satisfies_v) {
                    satisfies = satisfies && i;
                }
                
            } else if (logic == "OR") {
                satisfies = false;
                for (bool i: satisfies_v) {
                    satisfies = satisfies || i;
                }

            }

            if (satisfies) {
                priority = j;
                break;
            } 

        }

        priority_vec.push_back(priority);
    }

    /* Repeat with POLYMOD*/
    for (int i = 0; i < data->POLYMOD_nrow; i++) {
        /* Loop through each priority level */
        priority = -1;

        for (int j = 0; j < rules.size(); j++) {
            satisfies = true;
            /* Loop through each condition */
            for (int k = 0; k < rules[j].size(); k++) {

                colname = get<0>(rules[j][k]);
                condition = get<1>(rules[j][k]);

                if (colname == "hesitancy") {
                   satisfies *= bernoulli_distribution{stof(condition)}(generator); 
                } else if (data->POLYMOD(i, colname) == condition) {
                    satisfies *= true;
                } else {
                    satisfies *= false;
                }

            }

            if (satisfies) {
                priority = j;
                break;
            } 

        }

        priority_vec.push_back(priority);
    }

    return priority_vec;

}


/*
 * Function to parse params into rules
 *
 */
vector<rule> create_vax_rules(const char Colname[], const char Value[], const int n_conditions[], const int n_rules) {
    vector<rule> rules;
    rule temp_rule;

    // Get the number of rules
    // size_t n_rules = sizeof(n_conditions)/sizeof(n_conditions[0]);
    if (n_rules == 0) {return rules;}


    /* Accumulate colname and value into vectors using get_csv_row*/

    string Colname_s(Colname);
    string Value_s(Value);

    stringstream Colname_ss(Colname_s);
    vector<string> Colname_v = get_csv_row(Colname_ss, -1, ';');

    stringstream Value_ss(Value_s);
    vector<string> Value_v = get_csv_row(Value_ss, -1, ';');

    if (Colname_v.size() == 0 & Value_v.size() == 0) {
        return rules;
    }

    int idx = 0;
    for (int i = 0; i < n_rules; i++) {
        temp_rule = {};
        for (int j = 0; j < n_conditions[i]; j++) {

            temp_rule.push_back(make_tuple(Colname_v[idx], Value_v[idx]));
            idx++;
        }
        rules.push_back(temp_rule);
    }


    return rules;
}

/* 
 *
 * Generates a population of n households within graph g
 *
 */

void gen_pop_from_survey_csv(
        const Data *data, 
        igraph_t *g, 
        const Params *params) {

    bool verbose = false;
    bool fill_polymod = true;

    RandomVector dd(data->BICS_weights);
 
    mt19937 generator(params->POP_SEED);
    vector<rule> vax_rules = create_vax_rules(params->VAX_RULES_COLS, params->VAX_RULES_VALS, params->VAX_CONDS_N, params->VAX_RULES_N);
    vector<int> vax_vec = vaccine_priority(data, vax_rules, generator);

    cout << "Parsed the following vax rules: " << endl;
    for (int i = 0; i < vax_rules.size(); i++) {
        cout << "Priority no. " << i << ": ";
        for (int j = 0; j < vax_rules[i].size(); j++) {
            cout << get<0>(vax_rules[i][j]) << ": " << get<1>(vax_rules[i][j]) << "  " ;
        }

        cout << endl;
    }

    /* Tuple for keys*/ 
    typedef tuple <int,string,string> key;



    /* 
     * Draw n random nids to be the head of hh. Then,
     * booststrap the population to match the reported
     * hh_roster by hhsize, age, and gender.
     * Create a node for each within the passed graph.
     * 
     */
    for (int n = params->N_HH; n--; ) {
        // cout <<"Initializing new hh"<< endl;

        int hhead = dd(generator);
        // cout << hhead << endl;
        int hhsize = stoi(data->BICS(hhead,"hhsize")); 
        // cout << hhsize << endl;
        string hhid = randstring(16);

        if (verbose) cout << "Adding respondent " << hhead << " as head of hhid "<< hhid << " of size " << hhsize << endl;

        add_vertex(g,
                data->BICS(hhead, "age"),
                data->BICS(hhead, "gender"),
                data->BICS(hhead, "ethnicity"),
                stoi(data->BICS(hhead, "num_cc_nonhh")), 
                vax_vec[hhead],
                hhid);

        // Sample who matches that 
        for (int j = 1; j < min(5,hhsize); j++) {

            string hhmember_age = recode_age(data->BICS(hhead, "resp_hh_roster#1_" + to_string(j) + "_1"));
            string hhmember_gender = recode_gender(data->BICS(hhead,"resp_hh_roster#2_" + to_string(j)));

            if (hhmember_age == "") {cout<<"Not enough info on household member" << endl; continue;}


            // Create  vector of weights; set weight to 0 if doesn't match
            key k = key(hhsize, hhmember_age, hhmember_gender);

            // Check if key exists in map
            if (!data->hh_distn.count(k) & fill_polymod) {

                // Check if the key is in the POLYMOD distributions m.find("f") == m.end()
                if (data->distributions_POLYMOD.find(k) == data->distributions_POLYMOD.end()) {
                    if (verbose) cout << "Corresponding person not found in POLYMOD" << endl;

                    continue;

                }

                int pmod_member = data->distributions_POLYMOD.at(k)(generator); 

                if (verbose) cout << "Adding POLYMOD respondent " << pmod_member << " to hhid "<< hhid << endl;
                add_vertex(g,
                        data->POLYMOD(pmod_member, "part_age"),
                        data->POLYMOD(pmod_member,"part_gender"),
                        "NA", // POLYMOD lacks ethnicity data
                        stoi(data->POLYMOD(pmod_member, "num_cc_nonhh")), 
                        vax_vec[data -> BICS_nrow + pmod_member], // vax_vec contains BICS entries first, then polymod entries
                        hhid);


                continue;
            } 


            // Sample a corresponding person
            int hh_member = data->hh_distn.at(k)(generator) ;
            if (verbose) cout << "Adding respondent " << hh_member << " to hhid "<< hhid << endl;

            add_vertex(g,
                    data->BICS(hh_member,"age"),
                    data->BICS(hh_member, "gender"),
                    data->BICS(hh_member,"ethnicity"),
                    stoi(data->BICS(hh_member, "num_cc_nonhh")), 
                    vax_vec[hh_member],
                    hhid) ;

        }

    }


}



