// @@Example: ex_cpp_group_operator_equal @@
// @@Fold@@
#include <iostream>
#include <tightdb.hpp>

using namespace std;
using namespace tightdb;

int main()
{
// @@EndFold@@
    Group group_1("people_1.tightdb");
    Group group_2("people_2.tightdb");

    if (group_1 == group_2) cout << "EQUAL\n";
// @@Fold@@
}
// @@EndFold@@
// @@EndExample@@
