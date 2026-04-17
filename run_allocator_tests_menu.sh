#!/usr/bin/env bash
set -u -o pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -n "${BUILD_DIR:-}" ]]
then
  SELECTED_BUILD_DIR="$BUILD_DIR"
elif [[ -f "$ROOT_DIR/build/CMakeCache.txt" ]]
then
  SELECTED_BUILD_DIR="$ROOT_DIR/build"
else
  SELECTED_BUILD_DIR="$ROOT_DIR/build-check"
fi
BUILD_DIR="$SELECTED_BUILD_DIR"

SRTD_BIN="$BUILD_DIR/allocator/allocator_sorted_list/tests/sys_prog_allctr_allctr_srtd_lst_tests"
BNDR_BIN="$BUILD_DIR/allocator/allocator_boundary_tags/tests/sys_prog_allctr_allctr_bndr_tgs_tests"
BDDS_BIN="$BUILD_DIR/allocator/allocator_buddies_system/tests/sys_prog_allctr_allctr_bdds_sstm_tests"
RBIN_BIN="$BUILD_DIR/allocator/allocator_red_black_tree/tests/sys_prog_allctr_allctr_rb_tr_tests"
GLBL_BIN="$BUILD_DIR/allocator/allocator_global_heap/tests/sys_prog_allctr_allctr_glbl_hp_tests"

run_test_bin() {
  local bin="$1"
  local log_file
  log_file="$(mktemp)"
  "$bin" 2>&1 | tee "$log_file"
  local status=${PIPESTATUS[0]}
  if [[ $status -ne 0 ]] && grep -Eq "GLIBCXX_|GLIBC_" "$log_file"; then
    if [[ -x "$ROOT_DIR/run-in-noble.sh" ]]; then
      "$ROOT_DIR/run-in-noble.sh" "$bin"
      status=$?
    else
      echo "Нужен $ROOT_DIR/run-in-noble.sh для запуска в noble."
    fi
  fi
  rm -f "$log_file"
  return $status
}

while true
 do
  echo
  echo "Выбери тесты для запуска:"
  echo "1) allocator_sorted_list"
  echo "2) allocator_boundary_tags"
  echo "3) allocator_buddies_system"
  echo "4) allocator_red_black_tree"
  echo "5) allocator_global_heap"
  echo "6) все аллокаторы"
  echo "0) выход"
  read -r -p "Введите номер: " choice

  if [[ "$choice" == "0" ]]
  then
    exit 0
  fi

  if [[ ! -d "$BUILD_DIR" ]]
  then
    echo "Папка build-check не найдена. Сначала собери проект:"
    echo "cmake -S $ROOT_DIR -B $BUILD_DIR && cmake --build $BUILD_DIR -j"
    continue
  fi

  if [[ "$choice" == "1" ]]
  then
    if [[ -x "$SRTD_BIN" ]]
    then
      run_test_bin "$SRTD_BIN"
    else
      echo "Не найден бинарник: $SRTD_BIN"
    fi
  elif [[ "$choice" == "2" ]]
  then
    if [[ -x "$BNDR_BIN" ]]
    then
      run_test_bin "$BNDR_BIN"
    else
      echo "Не найден бинарник: $BNDR_BIN"
    fi
  elif [[ "$choice" == "3" ]]
  then
    if [[ -x "$BDDS_BIN" ]]
    then
      run_test_bin "$BDDS_BIN"
    else
      echo "Не найден бинарник: $BDDS_BIN"
    fi
  elif [[ "$choice" == "4" ]]
  then
    if [[ -x "$RBIN_BIN" ]]
    then
      run_test_bin "$RBIN_BIN"
    else
      echo "Не найден бинарник: $RBIN_BIN"
    fi
  elif [[ "$choice" == "5" ]]
  then
    if [[ -x "$GLBL_BIN" ]]
    then
      run_test_bin "$GLBL_BIN"
    else
      echo "Не найден бинарник: $GLBL_BIN"
    fi
  elif [[ "$choice" == "6" ]]
  then
    for bin in "$SRTD_BIN" "$BNDR_BIN" "$BDDS_BIN" "$RBIN_BIN" "$GLBL_BIN"
    do
      if [[ -x "$bin" ]]
      then
        echo "Запуск: $bin"
        run_test_bin "$bin"
      else
        echo "Не найден бинарник: $bin"
      fi
    done
  else
    echo "Неверный номер. Повтори ввод."
  fi
 done
