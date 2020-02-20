/*************************************************************************
 *
 * Copyright 2020 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#include "testsettings.hpp"
#ifdef TEST_LINKS

#include <realm.hpp>
#include <realm/util/file.hpp>
#include <realm/array_key.hpp>
#include <realm/history.hpp>

#include "test.hpp"

using namespace realm;
using namespace realm::util;
using namespace realm::test_util;

TEST(Unresolved_Basic)
{
    ObjKey k;

    CHECK_NOT(k);
    CHECK_NOT(k.get_unresolved());

    Group g;

    auto cars = g.add_table_with_primary_key("Car", type_String, "model");
    auto col_price = cars->add_column(type_Decimal, "price");
    auto persons = g.add_table_with_primary_key("Person", type_String, "e-mail");
    auto col_owns = persons->add_column_link(type_Link, "car", *cars);
    auto dealers = g.add_table_with_primary_key("Dealer", type_Int, "cvr");
    auto col_has = dealers->add_column_link(type_LinkList, "stock", *cars);

    auto finn = persons->create_object_with_primary_key("finn.schiermer-andersen@mongodb.com");
    auto mathias = persons->create_object_with_primary_key("mathias@10gen.com");
    auto joergen = dealers->create_object_with_primary_key(18454033);
    auto stock = joergen.get_linklist(col_has);

    auto skoda = cars->create_object_with_primary_key("Skoda Fabia").set(col_price, Decimal128("149999.5"));

    auto new_tesla = cars->get_objkey_from_primary_key("Tesla 10");
    CHECK(new_tesla.is_unresolved());
    finn.set(col_owns, new_tesla);
    mathias.set(col_owns, new_tesla);

    auto another_tesla = cars->get_objkey_from_primary_key("Tesla 10");
    stock.add(skoda.get_key());
    stock.add(another_tesla);

    CHECK_NOT(finn.get<ObjKey>(col_owns));
    CHECK(finn.is_unresolved(col_owns));
    CHECK(stock.has_unresolved());
    CHECK_EQUAL(stock.size(), 1);
    CHECK_EQUAL(stock.get(0), skoda.get_key());
    CHECK_EQUAL(cars->size(), 1);
    auto q = cars->column<Decimal128>(col_price) < Decimal128("300000");
    CHECK_EQUAL(q.count(), 1);

    // cars->dump_objects();
    auto tesla = cars->create_object_with_primary_key("Tesla 10").set(col_price, Decimal128("499999.5"));
    CHECK_EQUAL(tesla.get_backlink_count(), 3);
    CHECK_EQUAL(stock.size(), 2);
    CHECK_EQUAL(cars->size(), 2);
    CHECK(finn.get<ObjKey>(col_owns));

    // cars->dump_objects();
    tesla.invalidate();
    CHECK_EQUAL(stock.size(), 1);
    CHECK_EQUAL(stock.get(0), skoda.get_key());
    CHECK_EQUAL(cars->size(), 1);

    cars->create_object_with_primary_key("Tesla 10").set(col_price, Decimal128("499999.5"));
    CHECK_EQUAL(stock.size(), 2);
    CHECK_EQUAL(cars->size(), 2);
    CHECK(finn.get<ObjKey>(col_owns));
}


TEST(Unresolved_InvalidateObject)
{
    Group g;

    auto cars = g.add_table_with_primary_key("Car", type_String, "model");
    auto col_price = cars->add_column(type_Decimal, "price");
    auto dealers = g.add_table_with_primary_key("Dealer", type_Int, "cvr");
    auto col_has = dealers->add_column_link(type_LinkList, "stock", *cars);

    auto stock = dealers->create_object_with_primary_key(18454033).get_linklist(col_has);

    auto skoda = cars->create_object_with_primary_key("Skoda Fabia").set(col_price, Decimal128("149999.5"));
    auto tesla = cars->create_object_with_primary_key("Tesla 10").set(col_price, Decimal128("499999.5"));

    stock.add(tesla.get_key());
    stock.add(skoda.get_key());

    CHECK_EQUAL(stock.size(), 2);
    CHECK_EQUAL(cars->size(), 2);

    // Tesla goes to the grave. Too expensive
    cars->invalidate_object(tesla.get_key());

    auto tesla_key = cars->get_objkey_from_primary_key("Tesla 10");
    CHECK(tesla_key.is_unresolved());

    CHECK_EQUAL(stock.size(), 1);
    CHECK_EQUAL(stock.get(0), skoda.get_key());
    CHECK_EQUAL(cars->size(), 1);

    // resurrect the tesla
    cars->create_object_with_primary_key("Tesla 10").set(col_price, Decimal128("399999.5"));
    CHECK_EQUAL(stock.size(), 2);
    CHECK_EQUAL(cars->size(), 2);
}

TEST(Unresolved_LinkList)
{
    Group g;

    auto cars = g.add_table_with_primary_key("Car", type_String, "model");
    auto dealers = g.add_table_with_primary_key("Dealer", type_Int, "cvr");
    auto col_has = dealers->add_column_link(type_LinkList, "stock", *cars);

    auto dealer = dealers->create_object_with_primary_key(18454033);
    auto stock1 = dealer.get_linklist(col_has);
    auto stock2 = dealer.get_linklist(col_has);

    auto skoda = cars->create_object_with_primary_key("Skoda Fabia");
    auto tesla = cars->create_object_with_primary_key("Tesla 10");
    auto volvo = cars->create_object_with_primary_key("Volvo XC90");
    auto bmw = cars->create_object_with_primary_key("BMW 750");
    auto mercedes = cars->create_object_with_primary_key("Mercedes SLC500");

    stock1.add(skoda.get_key());
    stock1.add(tesla.get_key());
    stock1.add(volvo.get_key());
    stock1.add(bmw.get_key());

    CHECK_EQUAL(stock1.size(), 4);
    CHECK_EQUAL(stock2.size(), 4);
    tesla.invalidate();
    CHECK_EQUAL(stock1.size(), 3);
    CHECK_EQUAL(stock2.size(), 3);

    stock1.add(mercedes.get_key());
    // If REALM_MAX_BPNODE_SIZE is 4, we test that context flag is copied over when replacing root
    CHECK_EQUAL(stock1.size(), 4);
    CHECK_EQUAL(stock2.size(), 4);

    LnkLst stock_copy{stock1};
    CHECK_EQUAL(stock_copy.get(3), mercedes.get_key());
}

TEST(Unresolved_QueryOverLinks)
{
    Group g;

    auto cars = g.add_table_with_primary_key("Car", type_String, "model");
    auto col_price = cars->add_column(type_Decimal, "price");
    auto persons = g.add_table_with_primary_key("Person", type_String, "e-mail");
    auto col_owns = persons->add_column_link(type_Link, "car", *cars);
    auto dealers = g.add_table_with_primary_key("Dealer", type_Int, "cvr");
    auto col_has = dealers->add_column_link(type_LinkList, "stock", *cars);

    auto finn = persons->create_object_with_primary_key("finn.schiermer-andersen@mongodb.com");
    auto mathias = persons->create_object_with_primary_key("mathias@10gen.com");
    auto bilcentrum = dealers->create_object_with_primary_key(18454033);
    auto bilmekka = dealers->create_object_with_primary_key(26293995);
    auto skoda = cars->create_object_with_primary_key("Skoda Fabia").set(col_price, Decimal128("149999.5"));
    auto tesla = cars->create_object_with_primary_key("Tesla 3").set(col_price, Decimal128("449999.5"));
    auto volvo = cars->create_object_with_primary_key("Volvo XC90").set(col_price, Decimal128("1056000"));
    auto bmw = cars->create_object_with_primary_key("BMW 750").set(col_price, Decimal128("2088188"));
    auto mercedes = cars->create_object_with_primary_key("Mercedes SLC500").set(col_price, Decimal128("2355103"));

    finn.set(col_owns, skoda.get_key());
    mathias.set(col_owns, bmw.get_key());

    {
        auto stock = bilcentrum.get_linklist(col_has);
        stock.add(skoda.get_key());
        stock.add(tesla.get_key());
        stock.add(volvo.get_key());
    }
    {
        auto stock = bilmekka.get_linklist(col_has);
        stock.add(volvo.get_key());
        stock.add(bmw.get_key());
        stock.add(mercedes.get_key());
    }

    auto q = dealers->link(col_has).column<Decimal128>(col_price) < Decimal128("1000000");
    CHECK_EQUAL(q.count(), 1);

    auto new_tesla = cars->get_objkey_from_primary_key("Tesla 10");
    bilmekka.get_linklist(col_has).add(new_tesla);
    CHECK_EQUAL(q.count(), 1);

    q = persons->link(col_owns).column<Decimal128>(col_price) < Decimal128("1000000");
    CHECK_EQUAL(q.count(), 1);
    mathias.set(col_owns, new_tesla);
    CHECK_EQUAL(q.count(), 1);
}

TEST(Unresolved_PrimaryKeyInt)
{
    Group g;

    auto foo = g.add_table_with_primary_key("foo", type_Int, "id");
    auto bar = g.add_table("bar");
    auto col = bar->add_column_link(type_Link, "link", *foo);

    auto obj = bar->create_object();
    auto unres = foo->get_objkey_from_primary_key(5);
    obj.set(col, unres);
    CHECK_NOT(obj.get<ObjKey>(col));
    CHECK_EQUAL(foo->nb_unresolved(), 1);
    auto lazarus = foo->create_object_with_primary_key(5);
    CHECK_EQUAL(obj.get<ObjKey>(col), lazarus.get_key());
}

TEST(Unresolved_GarbageCollect)
{
    Group g;

    auto cars = g.add_table_with_primary_key("Car", type_String, "model");
    auto persons = g.add_table_with_primary_key("Person", type_String, "e-mail");
    auto col_owns = persons->add_column_link(type_Link, "car", *cars);

    auto finn = persons->create_object_with_primary_key("finn.schiermer-andersen@mongodb.com");
    auto mathias = persons->create_object_with_primary_key("mathias@10gen.com");

    auto new_tesla = cars->get_objkey_from_primary_key("Tesla 10");

    finn.set(col_owns, new_tesla);
    mathias.set(col_owns, new_tesla);
    CHECK_EQUAL(cars->nb_unresolved(), 1);
    finn.set_null(col_owns);
    CHECK_EQUAL(cars->nb_unresolved(), 1);
    mathias.set_null(col_owns);
    CHECK_EQUAL(cars->nb_unresolved(), 0);

    // Try the same with linklists. Here you have to modify the lists in order to
    // remove the unresolved links
    auto dealers = g.add_table_with_primary_key("Dealer", type_Int, "cvr");
    auto col_has = dealers->add_column_link(type_LinkList, "stock", *cars);
    auto bilcentrum = dealers->create_object_with_primary_key(18454033);
    auto bilmekka = dealers->create_object_with_primary_key(26293995);

    new_tesla = cars->get_objkey_from_primary_key("Tesla 10");

    bilcentrum.get_linklist(col_has).add(new_tesla);
    bilmekka.get_linklist(col_has).add(new_tesla);
    CHECK_EQUAL(cars->nb_unresolved(), 1);

    // create a real car
    auto skoda = cars->create_object_with_primary_key("Skoda Fabia");

    bilcentrum.get_linklist(col_has).add(skoda.get_key());
    CHECK_EQUAL(cars->nb_unresolved(), 1);
    bilmekka.get_linklist(col_has).add(skoda.get_key());
    CHECK_EQUAL(cars->nb_unresolved(), 0);
}

#endif