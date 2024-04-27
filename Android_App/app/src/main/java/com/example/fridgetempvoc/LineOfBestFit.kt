package com.example.fridgetempvoc

import kotlin.math.abs

class LineOfBestFit {
    private val dataSeries = mutableMapOf<Int, MutableList<Float>>()
    private var dialogFunction: (() -> Unit)? = null

    fun check(seriesId: Int, value: Float) {
        val seriesValues = dataSeries.getOrPut(seriesId) { mutableListOf() }

        if (seriesValues.size < 3) {
            seriesValues.add(value)
        } else {
            seriesValues.removeAt(0)
            seriesValues.add(value)
            calculateSlopeAndCheckDialog(seriesId)
        }
    }

    fun registerDialog(dialogFunction: () -> Unit) {
        this.dialogFunction = dialogFunction
    }

    private fun calculateSlopeAndCheckDialog(seriesId: Int) {
        val seriesValues = dataSeries[seriesId] ?: return

        val xSum = seriesValues.sum()
        val ySum = (1..seriesValues.size).sumBy { it }
        val xySum = seriesValues.mapIndexed { index, value -> (index + 1) * value }.sum()
        val xSquaredSum = seriesValues.mapIndexed { index, value -> (index + 1) * (index + 1) }.sum()

        val slope = (seriesValues.size * xySum - xSum * ySum) / (seriesValues.size * xSquaredSum - xSum * xSum)

        if (Math.abs(slope) > 1) {
            dialogFunction?.invoke()
        }
    }
}