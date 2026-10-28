-- File: tests/corpus/apps/sqlite3/inputs/dbfile.sql
-- Purpose: Create an on-disk database so the produced file's bytes are compared too
-- Author: Akilesh
-- Date: 2026-10-28
pragma page_size = 4096;
create table t(a integer primary key, b text);
insert into t(b) values ('one'), ('two'), ('three');
create index tb on t(b);
select count(*) from t;
pragma integrity_check;
