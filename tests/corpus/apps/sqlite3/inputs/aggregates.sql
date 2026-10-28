-- File: tests/corpus/apps/sqlite3/inputs/aggregates.sql
-- Purpose: Group, order and aggregate with floating-point results
-- Author: Akilesh
-- Date: 2026-10-28
create table sales(region text, item text, qty integer, price real);
insert into sales values
 ('north','pen',10,1.25),('north','ink',3,7.5),('south','pen',7,1.25),
 ('south','pad',4,3.1),('east','pen',1,1.25),('east','pad',9,3.1),('east','ink',2,7.5);
select region, count(*) n, sum(qty) q, round(sum(qty*price),2) revenue, round(avg(price),4) mean from sales group by region order by revenue desc;
select item, group_concat(region, '+') from sales group by item order by item;
select printf('%.3f %d %s %5.1f%%', 2.0/3, 42, 'x', 12.345);
select total(price), sum(price), avg(qty) from sales;
