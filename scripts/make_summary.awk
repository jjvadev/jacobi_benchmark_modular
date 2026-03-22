BEGIN {
    FS = ",";
    OFS = ",";
}
NR == 1 {
    next;
}
{
    key = $1 OFS $2 OFS $3 OFS $4;
    t = $6 + 0.0;
    count[key] += 1;
    sum[key] += t;
    sumsq[key] += t * t;
    if (!(key in minv) || t < minv[key]) minv[key] = t;
    if (!(key in maxv) || t > maxv[key]) maxv[key] = t;
}
END {
    print "implementation,n,nsweeps,workers,runs,avg_s,min_s,max_s,stddev_s";
    for (key in count) {
        avg = sum[key] / count[key];
        var = (sumsq[key] / count[key]) - (avg * avg);
        if (var < 0.0) var = 0.0;
        std = sqrt(var);
        print key, count[key], sprintf("%.6f", avg), sprintf("%.6f", minv[key]), sprintf("%.6f", maxv[key]), sprintf("%.6f", std);
    }
}
