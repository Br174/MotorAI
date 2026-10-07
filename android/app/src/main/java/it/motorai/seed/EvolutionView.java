package it.motorai.seed;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.View;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Lightweight, dependency-free evolution chart.
 * Values are percentages in [0,100].
 */
public class EvolutionView extends View {
    public static class Point {
        public final long timestampMs;
        public final int step;
        public final float learning;
        public final float memory;
        public final float evolution;
        public final String label;

        public Point(long timestampMs, int step, float learning, float memory, float evolution, String label) {
            this.timestampMs = timestampMs;
            this.step = step;
            this.learning = learning;
            this.memory = memory;
            this.evolution = evolution;
            this.label = label == null ? "" : label;
        }
    }

    private final List<Point> points = new ArrayList<>();
    private boolean timeAxis = false;
    private final Paint grid = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint axis = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint learningPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint memoryPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint evolutionPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint markerPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    public EvolutionView(Context context) { super(context); init(); }
    public EvolutionView(Context context, AttributeSet attrs) { super(context, attrs); init(); }

    private void init() {
        setMinimumHeight(dp(230));
        setBackgroundColor(Color.WHITE);

        grid.setColor(Color.rgb(225, 225, 225));
        grid.setStrokeWidth(dpF(1));

        axis.setColor(Color.rgb(90, 90, 90));
        axis.setStrokeWidth(dpF(1.2f));

        learningPaint.setColor(Color.rgb(40, 110, 220));
        learningPaint.setStyle(Paint.Style.STROKE);
        learningPaint.setStrokeWidth(dpF(2.2f));

        memoryPaint.setColor(Color.rgb(45, 155, 80));
        memoryPaint.setStyle(Paint.Style.STROKE);
        memoryPaint.setStrokeWidth(dpF(2.2f));

        evolutionPaint.setColor(Color.rgb(225, 135, 25));
        evolutionPaint.setStyle(Paint.Style.STROKE);
        evolutionPaint.setStrokeWidth(dpF(2.6f));

        textPaint.setColor(Color.rgb(80, 80, 80));
        textPaint.setTextSize(sp(11));

        markerPaint.setColor(Color.rgb(100, 100, 100));
        markerPaint.setStrokeWidth(dpF(1));
    }

    public void setPoints(List<Point> values) {
        points.clear();
        if (values != null) points.addAll(values);
        invalidate();
    }

    public void setTimeAxis(boolean enabled) {
        timeAxis = enabled;
        invalidate();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        final float left = dpF(42);
        final float right = getWidth() - dpF(12);
        final float top = dpF(26);
        final float bottom = getHeight() - dpF(42);
        final float width = Math.max(1, right - left);
        final float height = Math.max(1, bottom - top);

        for (int p = 0; p <= 100; p += 25) {
            float y = bottom - (p / 100f) * height;
            canvas.drawLine(left, y, right, y, grid);
            canvas.drawText(String.valueOf(p), dpF(5), y + dpF(4), textPaint);
        }
        canvas.drawLine(left, top, left, bottom, axis);
        canvas.drawLine(left, bottom, right, bottom, axis);

        drawLegend(canvas, left, dpF(13));

        if (points.isEmpty()) {
            canvas.drawText("Lo storico comparirà durante l'Auto-Training.", left, top + dpF(42), textPaint);
            return;
        }

        int minStep = points.get(0).step;
        int maxStep = points.get(points.size() - 1).step;
        if (maxStep <= minStep) maxStep = minStep + 1;

        long minTime = Long.MAX_VALUE;
        long maxTime = Long.MIN_VALUE;
        if (timeAxis) {
            for (Point p : points) {
                if (p.timestampMs <= 0L) continue;
                minTime = Math.min(minTime, p.timestampMs);
                maxTime = Math.max(maxTime, p.timestampMs);
            }
            if (minTime == Long.MAX_VALUE) {
                minTime = 0L;
                maxTime = 1L;
            } else if (maxTime <= minTime) {
                maxTime = minTime + 1L;
            }
        }

        Path lp = new Path();
        Path mp = new Path();
        Path ep = new Path();

        for (int i = 0; i < points.size(); i++) {
            Point p = points.get(i);
            float x;
            if (timeAxis && p.timestampMs > 0L) {
                x = left + ((p.timestampMs - minTime) / (float)(maxTime - minTime)) * width;
            } else {
                x = left + ((p.step - minStep) / (float)(maxStep - minStep)) * width;
            }
            float yl = bottom - clamp(p.learning) / 100f * height;
            float ym = bottom - clamp(p.memory) / 100f * height;
            float ye = bottom - clamp(p.evolution) / 100f * height;

            if (i == 0) {
                lp.moveTo(x, yl);
                mp.moveTo(x, ym);
                ep.moveTo(x, ye);
            } else {
                lp.lineTo(x, yl);
                mp.lineTo(x, ym);
                ep.lineTo(x, ye);
            }

            if (!p.label.isEmpty() && (i == points.size() - 1 || i == 0 || i % Math.max(1, points.size() / 5) == 0)) {
                canvas.drawLine(x, bottom, x, bottom + dpF(4), markerPaint);
                String label = p.label.length() > 12 ? p.label.substring(0, 12) : p.label;
                canvas.save();
                canvas.rotate(-35, x, bottom + dpF(12));
                canvas.drawText(label, x, bottom + dpF(12), textPaint);
                canvas.restore();
            }
        }

        canvas.drawPath(lp, learningPaint);
        canvas.drawPath(mp, memoryPaint);
        canvas.drawPath(ep, evolutionPaint);

        Point last = points.get(points.size() - 1);
        float lastX;
        if (timeAxis && last.timestampMs > 0L) {
            lastX = left + ((last.timestampMs - minTime) / (float)(maxTime - minTime)) * width;
        } else {
            lastX = left + ((last.step - minStep) / (float)(maxStep - minStep)) * width;
        }
        canvas.drawCircle(lastX, bottom - clamp(last.learning) / 100f * height, dpF(2.6f), learningPaint);
        canvas.drawCircle(lastX, bottom - clamp(last.memory) / 100f * height, dpF(2.6f), memoryPaint);
        canvas.drawCircle(lastX, bottom - clamp(last.evolution) / 100f * height, dpF(2.6f), evolutionPaint);

        if (timeAxis && minTime > 0L) {
            String start = formatTime(minTime, maxTime - minTime);
            String end = formatTime(maxTime, maxTime - minTime);
            canvas.drawText("Tempo " + start + " → " + end, left, getHeight() - dpF(5), textPaint);
        } else {
            canvas.drawText(String.format(Locale.ITALY, "Passi %d → %d", minStep, maxStep),
                    left, getHeight() - dpF(5), textPaint);
        }
    }

    private void drawLegend(Canvas canvas, float x, float y) {
        Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        fill.setStyle(Paint.Style.FILL);

        fill.setColor(learningPaint.getColor());
        canvas.drawCircle(x + dpF(5), y, dpF(4), fill);
        canvas.drawText("Apprendimento", x + dpF(13), y + dpF(4), textPaint);

        float x2 = x + dpF(105);
        fill.setColor(memoryPaint.getColor());
        canvas.drawCircle(x2 + dpF(5), y, dpF(4), fill);
        canvas.drawText("Memoria", x2 + dpF(13), y + dpF(4), textPaint);

        float x3 = x2 + dpF(82);
        fill.setColor(evolutionPaint.getColor());
        canvas.drawCircle(x3 + dpF(5), y, dpF(4), fill);
        canvas.drawText("Evoluzione", x3 + dpF(13), y + dpF(4), textPaint);
    }

    private String formatTime(long timestamp, long spanMs) {
        String pattern;
        if (spanMs <= 24L * 60L * 60L * 1000L) pattern = "HH:mm:ss";
        else if (spanMs <= 31L * 24L * 60L * 60L * 1000L) pattern = "dd/MM HH:mm";
        else pattern = "dd/MM/yy";
        return new SimpleDateFormat(pattern, Locale.ITALY).format(new Date(timestamp));
    }

    private static float clamp(float v) {
        return Math.max(0f, Math.min(100f, v));
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private float dpF(float v) {
        return v * getResources().getDisplayMetrics().density;
    }

    private float sp(float v) {
        return v * getResources().getDisplayMetrics().scaledDensity;
    }
}
