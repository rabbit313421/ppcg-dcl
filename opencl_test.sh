#!/bin/sh

keep=no
verbose=no

for option; do
	case "$option" in
		--keep)
			keep=yes
			;;
		--verbose)
			verbose=yes
			;;
	esac
done

EXEEXT=
VERSION=UNKNOWN
CC="gcc"
CFLAGS="--std=gnu99"
srcdir="."

if [ $keep = "yes" ]; then
	OUTDIR="opencl_test.$VERSION"
	mkdir "$OUTDIR" || exit 1
else
	if test "x$TMPDIR" = "x"; then
		TMPDIR=/tmp
	fi
	OUTDIR=`mktemp -d $TMPDIR/ppcg.XXXXXXXXXX` || exit 1
fi

run_tests () {
	subdir=$1
	ppcg_options=$2

	echo Test with PPCG options \'$ppcg_options\'
	mkdir ${OUTDIR}/${subdir} || exit 1
	for i in $srcdir/tests/*.c; do
		echo $i
		name=`basename $i`
		name="${name%.c}"
		out_c="${OUTDIR}/${subdir}/$name.ppcg.c"
		out_kernel_c="${OUTDIR}/${subdir}/$name.ppcg_kernel.c"
		out="${OUTDIR}/${subdir}/$name.ppcg$EXEEXT"
		options="--target=opencl --opencl-no-use-gpu $ppcg_options"
		functions="$srcdir/tests/${name}_opencl_functions.cl"
		if test -f $functions; then
			options="$options --opencl-include-file=$functions"
			options="$options --opencl-compiler-options=-I."
		fi
		if [ $verbose = "yes" ]; then
			echo "./ppcg$EXEEXT $options $i -o $out_c"
		fi
		./ppcg$EXEEXT $options $i -o "$out_c" || exit
		$CC $CFLAGS -I "$srcdir" "$srcdir/ocl_utilities.c" -lOpenCL \
			-I. "$out_c" "$out_kernel_c" -o "$out" || exit
		$out || exit
	done
}

run_tests default
run_tests embed --opencl-embed-kernel-code

for i in $srcdir/examples/*.c; do
	echo $i
	name=`basename $i`
	name="${name%.c}"
	exe_ref="${OUTDIR}/$name.ref$EXEEXT"
	gen_ocl="${OUTDIR}/$name.ppcg.c"
	gen_kernel_ocl="${OUTDIR}/$name.ppcg_kernel.c"
	exe_ocl="${OUTDIR}/$name.ppcg$EXEEXT"
	output_ref="${OUTDIR}/$name.ref.out"
	output_ocl="${OUTDIR}/$name.ppcg.out"
	$CC $CFLAGS $i -o $exe_ref || exit
	./ppcg$EXEEXT --target=opencl --opencl-no-use-gpu $i -o "$gen_ocl" || \
		exit
	$CC $CFLAGS -I "$srcdir" "$srcdir/ocl_utilities.c" -lOpenCL \
		"$gen_ocl" -I. "$gen_kernel_ocl" -o "$exe_ocl" || exit
	$exe_ref > $output_ref || exit
	$exe_ocl > $output_ocl || exit
	cmp $output_ref $output_ocl || exit
done

if [ $keep = "no" ]; then
	rm -r "${OUTDIR}"
fi
