-- File: tests/corpus/apps/sqlite3/inputs/dump.sql
-- Purpose: Exercise dot-commands: schema, dump and modes
-- Author: Akilesh
-- Date: 2026-10-28
create table a(x integer, y text);
insert into a values (1,'one'),(2,'two');
create view v as select * from a where x > 1;
.schema
.dump
.mode json
select * from a;
.mode line
select * from a limit 1;
