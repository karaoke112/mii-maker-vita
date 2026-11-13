// Generated from MiiJS data.js (childGenTables, backTables.switch.faceColors), ISC License,
// https://github.com/Stewared/MiiJS (see THIRD_PARTY_NOTICES.md). Do not edit by hand.
// For each hairstyle (0-131): the child hairstyle group for a boy and for a girl.
static const uint8_t kHairGroupFor[132][2] = {
    {2, 8}, {0, 6}, {0, 6}, {2, 8}, {2, 5}, {0, 4}, {2, 8}, {1, 9},
    {1, 9}, {0, 6}, {2, 8}, {0, 4}, {2, 7}, {0, 4}, {2, 8}, {1, 9},
    {2, 8}, {1, 9}, {2, 8}, {0, 6}, {2, 5}, {2, 5}, {2, 7}, {0, 5},
    {1, 9}, {2, 5}, {0, 6}, {2, 7}, {0, 6}, {1, 9}, {0, 4}, {1, 6},
    {0, 4}, {0, 4}, {1, 5}, {2, 5}, {1, 5}, {0, 4}, {1, 6}, {2, 5},
    {0, 4}, {2, 5}, {3, 9}, {1, 6}, {2, 7}, {1, 8}, {0, 6}, {0, 4},
    {2, 7}, {1, 6}, {2, 7}, {2, 4}, {0, 4}, {1, 9}, {3, 9}, {2, 7},
    {1, 6}, {1, 8}, {2, 7}, {1, 8}, {1, 6}, {0, 6}, {1, 4}, {0, 4},
    {1, 6}, {1, 8}, {0, 4}, {2, 8}, {3, 9}, {2, 8}, {2, 7}, {1, 9},
    {2, 5}, {1, 6}, {0, 4}, {1, 6}, {3, 9}, {0, 6}, {0, 6}, {1, 9},
    {3, 9}, {2, 7}, {2, 7}, {1, 9}, {0, 6}, {1, 9}, {2, 8}, {2, 7},
    {2, 8}, {3, 9}, {0, 4}, {0, 6}, {2, 5}, {2, 7}, {0, 4}, {2, 7},
    {1, 9}, {0, 6}, {0, 4}, {0, 4}, {2, 5}, {1, 9}, {0, 4}, {0, 6},
    {0, 6}, {2, 7}, {2, 7}, {0, 4}, {0, 4}, {0, 6}, {2, 7}, {0, 6},
    {1, 9}, {2, 5}, {0, 6}, {1, 6}, {2, 7}, {2, 7}, {3, 9}, {0, 9},
    {0, 4}, {1, 6}, {2, 5}, {2, 8}, {2, 5}, {1, 6}, {0, 6}, {1, 9},
    {2, 7}, {2, 7}, {0, 6}, {1, 9},
};
// Ten hairstyle groups (0-3 boys, 4-9 girls), each with four age groups of hairstyles.
struct HairList { uint8_t count; uint8_t types[16]; };
static const HairList kHairGroups[10][4] = {
    {{1, {30}}, {2, {45, 47}}, {3, {40, 33, 107}}, {4, {32, 37, 66, 52}}},
    {{1, {45}}, {4, {56, 38, 43, 49}}, {4, {115, 31, 87, 60}}, {5, {64, 121, 125, 73, 75}}},
    {{1, {51}}, {3, {55, 48, 51}}, {2, {44, 52}}, {8, {70, 50, 116, 58, 117, 22, 81, 87}}},
    {{1, {45}}, {2, {68, 62}}, {3, {76, 119, 118}}, {4, {42, 54, 89, 80}}},
    {{1, {30}}, {2, {51, 62}}, {4, {98, 11, 5, 94}}, {8, {63, 120, 90, 99, 13, 74, 108, 102}}},
    {{1, {45}}, {3, {35, 122, 21}}, {3, {113, 124, 25}}, {6, {100, 20, 4, 72, 21, 92}}},
    {{2, {45, 51}}, {2, {11, 21}}, {4, {46, 114, 61, 77}}, {16, {19, 104, 28, 9, 126, 78, 111, 97, 2, 103, 84, 130, 109, 26, 1, 91}}},
    {{1, {51}}, {2, {62, 122}}, {3, {106, 17, 12}}, {8, {82, 128, 129, 27, 93, 95, 105, 110}}},
    {{1, {45}}, {2, {122, 21}}, {3, {69, 16, 17}}, {7, {123, 3, 0, 10, 18, 14, 6}}},
    {{2, {30, 51}}, {3, {62, 122, 11}}, {3, {53, 83, 17}}, {13, {96, 101, 29, 79, 7, 15, 112, 127, 71, 24, 131, 8, 85}}},
};
// Skin colors (3DS 0-5) a child of two skin colors can have, [lower][higher], -1 = none.
static const int8_t kSkinMixing[6][6][4] = {
    {{0, -1, -1, -1}, {0, 1, -1, -1}, {0, 1, 2, 3}, {0, 3, -1, -1}, {1, 2, -1, -1}, {2, 4, -1, -1}},
    {{-1, -1, -1, -1}, {1, -1, -1, -1}, {1, 2, -1, -1}, {1, 3, -1, -1}, {2, -1, -1, -1}, {2, 4, -1, -1}},
    {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {2, -1, -1, -1}, {2, 3, -1, -1}, {2, 4, -1, -1}, {4, -1, -1, -1}},
    {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {3, -1, -1, -1}, {2, -1, -1, -1}, {2, 4, -1, -1}},
    {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {4, -1, -1, -1}, {4, 5, -1, -1}},
    {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {5, -1, -1, -1}},
};
// Switch skin colors (0-9) as the 3DS ones (0-5) the table above is for.
static const uint8_t kSkinTo3ds[10] = {0, 1, 2, 3, 4, 5, 0, 1, 5, 5};
static const char* const kBoysNames[] = {
    "Aaron", "Adam", "Adrian", "Aiden", "Ayden", "Alex", "Alexander", "Alfie", "Andrew", "Anthony", "Archie",
    "Austin", "Ben", "Benjamin", "Bentley", "Bill", "Billy", "Blake", "Bradley", "Brandon", "Brayden",
    "Brody", "Bryson", "Caleb", "Callum", "Cameron", "Carlos", "Charlie", "Charles", "Carson", "Carter",
    "Chase", "Chris", "Christian", "Cody", "Colton", "Connor", "Cooper", "Damian", "Daniel", "David",
    "Dexter", "Dominic", "Dylan", "Easton", "Edward", "Eli", "Elijah", "Elliot", "Ethan", "Evan", "Finlay",
    "Frankie", "Freddie", "Gabriel", "Gavin", "George", "Grayson", "Harrison", "Harvey", "Henry", "Hudson",
    "Hugo", "Hunter", "Ian", "Isaac", "Isaiah", "Jace", "Jack", "Jackson", "Jaxon", "Jacob", "Jake", "James",
    "Jason", "Jayden", "Jenson", "Jeremiah", "John", "Juan", "Jonathan", "Jordan", "Jose", "Joseph", "Josiah",
    "Joshua", "Jude", "Julian", "Justin", "Kai", "Kayden", "Kevin", "Kian", "Landon", "Levi", "Leo", "Logan",
    "Lucas", "Luke", "Luis", "Lachlan", "Mason", "Matthew", "Max", "Michael", "Miguel", "Nathan", "Nathaniel",
    "Nicholas", "Noah", "Nolan", "Olly", "Oliver", "Owen", "Parker", "Philip", "Rhys", "Reece", "Rob",
    "Robert", "Ryan", "Ryder", "Samuel", "Sebastian", "Seth", "Thomas", "Tommy", "Trent", "Tristan", "Tyler",
    "William", "Liam", "Wyatt", "Xavier", "Zac", "Zachary", "Alex", "Alexis", "Angel", "Bailey", "Darcy",
    "Darcey", "Genesis", "Kennedy", "Mackenzie", "Morgan", "Peyton", "Sam", "Taylor",
};
static const char* const kGirlsNames[] = {
    "Aaliyah", "Abigail", "Addison", "Madison", "Maddison", "Alexa", "Alexandra", "Alison", "Allison",
    "Alyssa", "Amelia", "Amy", "Andrea", "Anna", "Annabelle", "Aria", "Ariana", "Arianna", "Ashley", "Aubree",
    "Aubrey", "Audrey", "Autumn", "Ava", "Avery", "Bella", "Bethany", "Brianna", "Brooklyn", "Camila",
    "Caroline", "Charlotte", "Chloe", "Khloe", "Claire", "Ella", "Ellie", "Elenor", "Elizabeth", "Lizabeth",
    "Liza", "Emily", "Emma", "Eva", "Evie", "Evelyn", "Faith", "Gabriella", "Gianna", "Grace", "Hailey",
    "Hannah", "Harper", "Heidi", "Hollie", "Holly", "Isabella", "Isobel", "Jasmine", "Jessica", "Jocelyn",
    "Julia", "Katherine", "Kayla", "Kaylee", "Kimberly", "Kylie", "Lacey", "Lauren", "Layla", "Leah", "Lexie",
    "Lilian", "Lily", "Lola", "London", "Lucy", "Lydia", "Madeline", "Madelyn", "Maisie", "Makayla", "Maya",
    "Mya", "Megan", "Melanie", "Mia", "Molly", "Naomi", "Natalie", "Nevaeh", "Olivia", "Paige", "Poppy",
    "Piper", "Reagan", "Rebecca", "Riley", "Rosie", "Samantha", "Sarah", "Savannah", "Scarlett", "Serenity",
    "Skye", "Skylar", "Sofia", "Sophia", "Sophie", "Spring", "Stella", "Summer", "Sydney", "Trinity",
    "Vanessa", "Victoria", "Violet", "Winter", "Zara", "Zoe", "Zoey", "Alex", "Alexis", "Angel", "Bailey",
    "Darcy", "Darcey", "Genesis", "Kennedy", "Mackenzie", "Morgan", "Peyton", "Sam", "Taylor",
};
