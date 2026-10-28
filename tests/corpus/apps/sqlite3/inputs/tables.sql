-- File: tests/corpus/apps/sqlite3/inputs/tables.sql
-- Purpose: Create tables, insert rows and query them back
-- Author: Akilesh
-- Date: 2026-10-28
create table people(id integer primary key, name text not null, age integer, score real);
insert into people(name, age, score) values ('Ada', 36, 91.5), ('Grace', 45, 88.25), ('Linus', 54, 77.0), ('Margaret', 40, null);
select * from people order by age desc;
select name, score * 2 from people where score is not null;
update people set age = age + 1 where name like 'G%';
delete from people where id = 3;
select count(*), min(age), max(age) from people;
select 'done';
