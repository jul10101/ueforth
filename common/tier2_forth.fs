\ Copyright 2022 Bradley D. Nelson
\
\ Licensed under the Apache License, Version 2.0 (the "License");
\ you may not use this file except in compliance with the License.
\ You may obtain a copy of the License at
\
\     http://www.apache.org/licenses/LICENSE-2.0
\
\ Unless required by applicable law or agreed to in writing, software
\ distributed under the License is distributed on an "AS IS" BASIS,
\ WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
\ See the License for the specific language governing permissions and
\ limitations under the License.

( Dictionary Format )
: >flags& ( xt -- a ) cell - ; : >flags ( xt -- flags ) >flags& c@ ;
: >name-length ( xt -- n ) >flags& 1+ c@ ;
: >params ( xt -- n ) >flags& 2 + sw@ $ffff and ;
: >size ( xt -- n ) dup >params cells swap >name-length aligned + 3 cells + ;
: >link& ( xt -- a ) 2 cells - ;   : >link ( xt -- a ) >link& @ ;
: >name ( xt -- a n ) dup >name-length swap >link& over aligned - swap ;
: >body ( xt -- a ) dup @ [ ' >flags @ ] literal = 2 + cells + ;

: aligned ( a -- a ) cell 1 - dup >r + r> invert and ;
: align   here aligned here - allot ;

: fill32 ( a n v ) swap >r swap r> 0 ?do 2dup ! cell+ loop 2drop ;
