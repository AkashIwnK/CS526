; ModuleID = 'cast_struct.ll'
source_filename = "cast_struct.c"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.RT = type { i32, i32 }
%struct.ST = type { float*, float* }

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = alloca %struct.RT, align 4
  %2 = getelementptr inbounds %struct.RT, %struct.RT* %1, i32 0, i32 0
  %3 = bitcast i32* %2 to float*
  %4 = getelementptr inbounds %struct.ST, %struct.ST* undef, i32 0, i32 0
  %5 = getelementptr inbounds %struct.RT, %struct.RT* %1, i32 0, i32 1
  %6 = bitcast i32* %5 to float*
  %7 = getelementptr inbounds %struct.ST, %struct.ST* undef, i32 0, i32 1
  ret i32 0
}

attributes #0 = { noinline nounwind optnone uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 8.0.1 (tags/RELEASE_801/final)"}
