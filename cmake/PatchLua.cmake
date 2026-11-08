# Adds the `continue` statement to the fetched Lua 5.0.2 sources.
#
# The game's own Lua has a reserved word `continue` (between `break` and `else` in its token table, found in the Mac
# binary), and Reforged's sim/port.lua uses it: `if port == playerPort then continue end`. Stock 5.0.2 cannot parse
# that file. `continue` jumps to the end of the enclosing loop body (the next iteration), the way `break` leaves the
# loop. Idempotent: applied once to the extracted source under the build directory, never to the pinned tarball.
#
# The lexer also reads hexadecimal numerals (community/json.lua has [0x80] = 0x20AC), see patch_lua_hex below.
#
# usage: patch_lua(<lua source dir>)
function(replace_once file from to)
  file(READ "${file}" text)
  string(FIND "${text}" "${from}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "PatchLua: '${from}' not found in ${file}")
  endif()
  string(REPLACE "${from}" "${to}" text "${text}")
  file(WRITE "${file}" "${text}")
endfunction()

function(patch_lua srcdir)
  patch_lua_continue("${srcdir}")
  patch_lua_hex("${srcdir}")
  patch_lua_undump32("${srcdir}")
  patch_lua_defaultmeta("${srcdir}")
  patch_lua_resumehook("${srcdir}")
endfunction()

function(patch_lua_continue srcdir)
  file(READ "${srcdir}/src/llex.h" llex_h)
  if(llex_h MATCHES "TK_CONTINUE")
    return()
  endif()

  replace_once("${srcdir}/src/llex.h" "TK_AND = FIRST_RESERVED, TK_BREAK," "TK_AND = FIRST_RESERVED, TK_BREAK, TK_CONTINUE,")
  replace_once("${srcdir}/src/llex.c" "\"and\", \"break\", \"do\", \"else\", \"elseif\"," "\"and\", \"break\", \"continue\", \"do\", \"else\", \"elseif\",")

  set(P "${srcdir}/src/lparser.c")
  replace_once("${P}" "  int breaklist;  /* list of jumps out of this loop */"
                      "  int breaklist;  /* list of jumps out of this loop */\n  int continuelist;  /* list of jumps to the end of the loop body */")
  replace_once("${P}" "  bl->breaklist = NO_JUMP;" "  bl->breaklist = NO_JUMP;\n  bl->continuelist = NO_JUMP;")
  replace_once("${P}" "static int statement (LexState *ls) {"
"static void continuestat (LexState *ls) {
  /* stat -> CONTINUE */
  FuncState *fs = ls->fs;
  BlockCnt *bl = fs->bl;
  int upval = 0;
  next(ls);  /* skip CONTINUE */
  while (bl && !bl->isbreakable) {
    upval |= bl->upval;
    bl = bl->previous;
  }
  if (!bl)
    luaX_syntaxerror(ls, \"no loop to continue\");
  if (upval)
    luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);
  luaK_concat(fs, &bl->continuelist, luaK_jump(fs));
}


static int statement (LexState *ls) {")
  replace_once("${P}" "    default: {\n      exprstat(ls);"
                      "    case TK_CONTINUE: {  /* stat -> continuestat */\n      continuestat(ls);\n      return 0;\n    }\n    default: {\n      exprstat(ls);")
  replace_once("${P}" "  block(ls);\n  luaK_patchtohere(fs, whileinit);" "  block(ls);\n  luaK_patchtohere(fs, bl.continuelist);\n  luaK_patchtohere(fs, whileinit);")
  replace_once("${P}" "  block(ls);\n  check_match(ls, TK_UNTIL, TK_REPEAT, line);" "  block(ls);\n  luaK_patchtohere(fs, bl.continuelist);\n  check_match(ls, TK_UNTIL, TK_REPEAT, line);")
  replace_once("${P}" "  block(ls);\n  luaK_patchtohere(fs, prep-1);" "  block(ls);\n  luaK_patchtohere(fs, bl.continuelist);\n  luaK_patchtohere(fs, prep-1);")
endfunction()

# Hexadecimal numerals, as the game's read_numeral does (Mac binary 0xf3ab5): after a leading '0', a lowercase 'x'
# (not 'X') starts a hex numeral. The character after the 'x' is always consumed, then up to 8 characters in all, ending
# early at the first non-hex character. Digits are 0-9 and a-f in either case. The value is an unsigned 32-bit integer
# (0xFFFFFFFF is 4294967295); there are no fractions or exponents and a ninth digit starts a new token.
function(patch_lua_hex srcdir)
  file(READ "${srcdir}/src/llex.c" llex_c)
  if(llex_c MATCHES "choco hex numerals")
    return()
  endif()
  replace_once("${srcdir}/src/llex.c" "  if (comma) save(LS, '.', l);
  while (isdigit(LS->current)) {"
"  if (comma) save(LS, '.', l);
  if (!comma && LS->current == '0') {  /* choco hex numerals */
    next(LS);
    if (LS->current == 'x') {
      unsigned long v = 0;
      int n = 0;
      next(LS);
      for (;;) {
        int c = tolower(LS->current);
        if (c >= '0' && c <= '9') v = v * 16 + (unsigned long)(c - '0');
        else if (c >= 'a' && c <= 'f') v = v * 16 + (unsigned long)(c - 'a' + 10);
        next(LS);
        n++;
        c = tolower(LS->current);
        if (n == 8 || !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) break;
      }
      seminfo->r = (lua_Number)(v & 0xFFFFFFFFUL);
      return;
    }
    save(LS, '0', l);
  }
  while (isdigit(LS->current)) {")
endfunction()

# Vanilla's chunks are Lua 5.0 bytecode written by a 32-bit build, with the game's shifted opcode numbering: size_t and Instruction are 4 bytes in the file. A
# 64-bit build (LP64: size_t 8, Instruction = unsigned long 8) rejects them in stock lundump.c. The header's size_t and
# Instruction widths may now be 4 or the native width; strings lengths and code words of width 4 are widened on load.
# int, the opcode field sizes and lua_Number must still match natively; any other size_t or Instruction width is
# reported with the byte already read. The loader expects the game's opcode dialect, so chunks written by stock-numbered
# luac or string.dump do not load.
function(patch_lua_undump32 srcdir)
  file(READ "${srcdir}/src/lundump.c" text)
  if(text MATCHES "choco 32-bit chunks v2")
    return()
  elseif(text MATCHES "choco 32-bit chunks")
    # An older version of this patch is in the fetched source; it cannot be re-patched in place.
    message(FATAL_ERROR "PatchLua: ${srcdir}/src/lundump.c carries an older 32-bit chunk patch. Delete FetchContent's "
                        "lua502 directories in this build tree (_deps/lua502-src, lua502-build and lua502-subbuild, so "
                        "the tarball is extracted again) and configure again.")
  endif()
  set(U "${srcdir}/src/lundump.c")
  replace_once("${U}" [=[ int swap;
]=] [=[ int swap;
 int sizet;  /* choco 32-bit chunks v2: widths of size_t and Instruction in the file */
 int insz;
]=])
  replace_once("${U}" [=[ size_t x;
 LoadBlock(S,&x,sizeof(x));
 return x;]=] [=[ size_t x;
 if (S->sizet==4 && sizeof(x)!=4)  /* choco 32-bit chunks */
 {
  unsigned int y;
  LoadBlock(S,&y,4);
  return y;
 }
 LoadBlock(S,&x,sizeof(x));
 return x;]=])
  replace_once("${U}" [=[ LoadVector(S,f->code,size,sizeof(*f->code));]=] [=[ if (S->insz==4 && sizeof(Instruction)!=4)  /* choco 32-bit chunks */
 {
  int i;
  for (i=0; i<size; i++)
  {
   unsigned int y;
   LoadBlock(S,&y,4);
   f->code[i]=y;
  }
 }
 else
  LoadVector(S,f->code,size,sizeof(*f->code));
 /* choco opcode numbering: the game's compiler numbers every opcode from POW on 4 higher than stock 5.0.2 (docs/re/
    embedded-lua.md section 3). Chunks are always in that dialect, so map 20..38 back to 16..34 and refuse 16..19 and anything above 38,
    which the compiler never emits. */
 {
  int i;
  for (i=0; i<size; i++)
  {
   unsigned long op=f->code[i]&0x3F;
   if ((op>=16 && op<20) || op>38) luaG_runerror(S->L,"bad opcode %d in %s",(int)op,S->name);
   if (op>=20) f->code[i]-=4;
  }
 }]=])
  replace_once("${U}" [=[ TESTSIZE(sizeof(size_t), "size_t");
 TESTSIZE(sizeof(Instruction), "Instruction");]=] [=[ /* choco 32-bit chunks: size_t and Instruction may be 4 bytes or native */
 S->sizet=LoadByte(S);
 if (S->sizet!=4 && S->sizet!=(int)sizeof(size_t))
  luaG_runerror(S->L,"virtual machine mismatch in %s: size of %s is %d but read %d",S->name,"size_t",(int)sizeof(size_t),S->sizet);
 S->insz=LoadByte(S);
 if (S->insz!=4 && S->insz!=(int)sizeof(Instruction))
  luaG_runerror(S->L,"virtual machine mismatch in %s: size of %s is %d but read %d",S->name,"Instruction",(int)sizeof(Instruction),S->insz);]=])
endfunction()

# Per-type default metatables, as the game's Lua has them (docs/re/embedded-lua.md section 7).
#
# global_State carries TObject defmt[11], indexed by type tag. f_luaopen leaves defmt[0] (nil) nil and gives defmt[1..10]
# each a fresh empty table. luaT_gettmbyobj answers, for any type but table and userdata, the defmt table itself (for
# every event), so luaV_gettable/luaV_settable index or store into that table, and only nil raises a type error. The
# only table ever installed is the string library for strings (lua_setdefaultmetatable, called by the host after
# luaopen_string, script/LuaCompat.h). Stock luaV_getnotable/luaV_settable already follow a non-nil result of
# luaT_gettmbyobj, so lvm.c needs no change. No function is ever produced for non-table types, so e.g. `"a" .. t`
# calls the string table and raises, as in the game. Idempotent via the marker below; an older patch cannot be
# re-applied in place.
#
# usage: patch_lua_defaultmeta(<lua source dir>)
function(patch_lua_defaultmeta srcdir)
  file(READ "${srcdir}/src/lstate.h" lstate_h)
  file(READ "${srcdir}/src/ltm.c" ltm_c)
  if(lstate_h MATCHES "choco default metatables v2")
    return()
  elseif(lstate_h MATCHES "choco default metatables" OR ltm_c MATCHES "choco string meta")
    message(FATAL_ERROR "PatchLua: ${srcdir} carries an older string/default metatable patch. Delete FetchContent's "
                        "lua502 directories in this build tree (_deps/lua502-src, lua502-build and lua502-subbuild, so "
                        "the tarball is extracted again) and configure again.")
  endif()
  set(S "${srcdir}/src")
  replace_once("${S}/lstate.h" "  TObject _defaultmeta;
" "  TObject _defaultmeta;
  TObject defmt[11];  /* choco default metatables v2: per-type default metatable, indexed by type tag */
")
  replace_once("${S}/lstate.c" "  setnilvalue(defaultmeta(L));
  setnilvalue(registry(L));
" "  setnilvalue(defaultmeta(L));
  setnilvalue(registry(L));
  { int i; for (i=0; i<11; i++) setnilvalue(&g->defmt[i]); }  /* choco default metatables v2 */
")
  replace_once("${S}/lstate.c" "  sethvalue(registry(L), luaH_new(L, 4, 4));  /* registry */
" "  sethvalue(registry(L), luaH_new(L, 4, 4));  /* registry */
  { int i; for (i=1; i<11; i++) sethvalue(&g->defmt[i], luaH_new(L, 0, 0)); }  /* choco default metatables v2 */
")
  replace_once("${S}/lgc.c" "  markobject(st, registry(L));
" "  markobject(st, registry(L));
  { int i; for (i=0; i<11; i++) markobject(st, &g->defmt[i]); }  /* choco default metatables v2 */
")
  replace_once("${S}/ltm.c" "    default:
      return &luaO_nilobject;" "    default:  /* choco default metatables v2: the per-type default metatable itself, for every event */
      return &G(L)->defmt[ttype(o)];")
  replace_once("${S}/lapi.c" "        mt = uvalue(obj)->uv.metatable;
        break;
    }
  }
  if (mt == NULL" "        mt = uvalue(obj)->uv.metatable;
        break;
      default:  /* choco default metatables v2 */
        if (ttistable(&G(L)->defmt[ttype(obj)])) mt = hvalue(&G(L)->defmt[ttype(obj)]);
        break;
    }
  }
  if (mt == NULL")
  replace_once("${S}/lapi.c" "/*
** set functions (stack -> Lua)
*/
" "/* choco default metatables v2: the game's lua_getdefaultmetatable / lua_setdefaultmetatable */
LUA_API void lua_getdefaultmetatable (lua_State *L, int t) {
  lua_lock(L);
  api_check(L, 0 <= t && t < 11);
  setobj2s(L->top, &G(L)->defmt[t]);
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API void lua_setdefaultmetatable (lua_State *L, int t) {
  lua_lock(L);
  api_checknelems(L, 1);
  api_check(L, 0 <= t && t < 11);
  if (ttistable(L->top - 1)) setobj(&G(L)->defmt[t], L->top - 1);
  L->top--;
  lua_unlock(L);
}


/*
** set functions (stack -> Lua)
*/
")
  replace_once("${srcdir}/include/lua.h" "LUA_API void  lua_getfenv (lua_State *L, int idx);
" "LUA_API void  lua_getfenv (lua_State *L, int idx);
LUA_API void  lua_getdefaultmetatable (lua_State *L, int t);  /* choco default metatables v2 */
LUA_API void  lua_setdefaultmetatable (lua_State *L, int t);
")
endfunction()

# A host hook called when an error is about to unwind a coroutine run by lua_resume (the error is not caught by a pcall
# inside the coroutine). It runs from luaD_throw, while the failing call frames are still live: lua_resume only reports
# the error after longjmp has left luaV_execute, by which time the saved-pc pointers of the Lua frames dangle and
# lua_getinfo may crash (and lua_resume itself resets L->ci to base_ci). Inside the hook lua_getstack / lua_getinfo see the
# frames that raised, so the host can take a traceback of a coroutine that is about to die. The patch only adds code.
# Idempotent via the marker below.
#
# usage: patch_lua_resumehook(<lua source dir>)
function(patch_lua_resumehook srcdir)
  file(READ "${srcdir}/src/ldo.c" ldo_c)
  if(ldo_c MATCHES "choco resume hook v1")
    return()
  endif()
  set(S "${srcdir}/src")
  replace_once("${S}/ldo.c" "void luaD_throw (lua_State *L, int errcode) {
  if (L->errorJmp) {"
"/* choco resume hook v1: called with the coroutine whose error nothing inside it will catch, frames still live */
void (*lua_resumeerrorhook) (lua_State *L) = NULL;


void luaD_throw (lua_State *L, int errcode) {
  if (L->errorJmp && L->errorJmp->previous == NULL && lua_resumeerrorhook)
    lua_resumeerrorhook(L);  /* outermost handler = lua_resume's: this error ends the coroutine */
  if (L->errorJmp) {")
  replace_once("${srcdir}/include/lua.h" "LUA_API void  lua_getfenv (lua_State *L, int idx);
" "LUA_API void  lua_getfenv (lua_State *L, int idx);
extern void (*lua_resumeerrorhook) (lua_State *L);  /* choco resume hook v1 */
")
endfunction()
