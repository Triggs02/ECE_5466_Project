package com.example.fridgetempvoc

import android.annotation.SuppressLint
import android.app.AlertDialog
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.annotation.RequiresApi
import androidx.appcompat.app.AppCompatActivity
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import java.time.Instant
import kotlinx.serialization.*
import kotlinx.serialization.json.Json


class MainActivity : AppCompatActivity() {

    private lateinit var webSocketClient: WebSocketClient
    private lateinit var lineOfBestFit: LineOfBestFit
    private lateinit var vocConcentrationChart: LineChart
    private lateinit var tempChart: LineChart
    private var vocDataset = mutableListOf<LineDataSet>(LineDataSet(mutableListOf<Entry>(), "VOC Fridge Top"), LineDataSet(mutableListOf<Entry>(), "VOC Fridge Bottom"))
    private var tempDataset = mutableListOf<LineDataSet>(LineDataSet(mutableListOf<Entry>(), "Temp Fridge Top"), LineDataSet(mutableListOf<Entry>(), "Temp Fridge Bottom"))
    private var counts = mutableListOf<Float>(5f, 5f)

    private val socketListener = object : WebSocketClient.SocketListener {
        @RequiresApi(Build.VERSION_CODES.O)
        @SuppressLint("SetTextI18n")
        override fun onMessage(message: String) {
            Log.e("socketCheck onMessage", message)
            if (message.startsWith("Data")) {
                val final_message = message.substring(4)
                val parts = final_message.split(";")
                var voc = parts[0].toFloat()
                var temp = parts[1].toFloat()
                var battLvl = parts[2].toInt()
                var client = parts[3].toInt()

                val targetClient = 0xDC

                if (client == targetClient) {
                    try {
                        val currentTime = Instant.now().epochSecond
                        Log.e("current time", currentTime.toString())
                        vocDataset[0].addEntry(Entry(counts[0], voc))
                        tempDataset[0].addEntry(Entry(counts[0], temp))

                        counts[0] += 5f

                        lineOfBestFit.check(client, voc)

                        val lineData = LineData()
                        val lineData2 = LineData()
                        for (vocData in vocDataset) {
                            lineData.addDataSet(vocData)

                        }
                        for (tempData in tempDataset){
                            lineData2.addDataSet(tempData)
                        }

                        vocConcentrationChart.data = lineData
                        tempChart.data = lineData2

                        vocConcentrationChart.notifyDataSetChanged()
                        tempChart.notifyDataSetChanged()

                        vocConcentrationChart.invalidate()
                        tempChart.invalidate()

                        val vocText = findViewById<View>(R.id.VOCConcentration) as TextView
                        vocText.text = voc.toString() + "mg/m^3"

                        val tempText = findViewById<View>(R.id.temperature) as TextView
                        tempText.text = temp.toString() + "C"
                    } catch (e: Exception){
                        Log.e("Error with text", e.toString())
                    }
                } else {
                    try {
                        vocDataset[1].addEntry(Entry(counts[1], voc))
                        tempDataset[1].addEntry(Entry(counts[1], temp))

                        counts[1] += 5f

                        val lineData = LineData()
                        val lineData2 = LineData()
                        for (vocData in vocDataset) {
                            lineData.addDataSet(vocData)

                        }
                        for (tempData in tempDataset){
                            lineData2.addDataSet(tempData)
                        }

                        vocConcentrationChart.data = lineData
                        tempChart.data = lineData2

                        vocConcentrationChart.notifyDataSetChanged()
                        tempChart.notifyDataSetChanged()

                        vocConcentrationChart.invalidate()
                        tempChart.invalidate()
                    } catch (e: Exception){
                        Log.e("Error with text", e.toString())
                    }
                }
            } else if (message.startsWith("System")) {
                val final_message = message.substring(6)
                val parts = final_message.split(";")
                var sensorIds = parts[0].split(",")

                SensorAlertDialog(sensorIds.size, listOf("Top Sensor", "Bottom Sensor"))
            }
        }

    }

    fun VOCAlertDialog() {
        val builder = AlertDialog.Builder(this)
        builder.setTitle("Elevated VOC")
        builder.setMessage("Detected elevated VOC levels in your fridge. Please check its contents for spoilage!!'")
//builder.setPositiveButton("OK", DialogInterface.OnClickListener(function = x))

        builder.setPositiveButton(android.R.string.yes) { dialog, which ->
            Toast.makeText(applicationContext,
                android.R.string.yes, Toast.LENGTH_SHORT).show()
        }

        builder.setNeutralButton(android.R.string.cancel) { dialog, which ->
            Toast.makeText(applicationContext,
                android.R.string.cancel, Toast.LENGTH_SHORT).show()
        }

        builder.show()
    }

    fun generateNameString(names: List<String>): String {
        return when {
            names.isEmpty() -> ""
            names.size == 1 -> names[0]
            else -> {
                val allButLast = names.dropLast(1).joinToString(", ")
                val last = names.last()
                "$allButLast and $last"
            }
        }
    }
    fun SensorAlertDialog(numberOfSensors: Int, names: List<String>) {
        val builder = AlertDialog.Builder(this)
        builder.setTitle("Sensor Detect")

        builder.setMessage("We detected $numberOfSensors sensors connected to your base station and named them ${generateNameString(names)}")

        builder.setPositiveButton(android.R.string.yes) { _, _ ->
            Toast.makeText(applicationContext,
                android.R.string.yes, Toast.LENGTH_SHORT).show()
        }

        builder.show()
    }

    @RequiresApi(Build.VERSION_CODES.O)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        if (intent.getStringExtra("url") == null) {
            val intent = Intent(this, WebsocketLogin::class.java)
            startActivity(intent)
        }

        lineOfBestFit = LineOfBestFit()


        lineOfBestFit.registerDialog{VOCAlertDialog()}


        webSocketClient = WebSocketClient.getInstance()
        webSocketClient.setSocketUrl(intent.getStringExtra("url")!!)
        webSocketClient.setListener(socketListener)
        webSocketClient.connect()

        vocConcentrationChart = findViewById<View>(R.id.voc_concentration_chart) as LineChart
        tempChart = findViewById<View>(R.id.temperature_chart) as LineChart

        vocConcentrationChart.setVisibleXRangeMaximum(25F)
        tempChart.setVisibleXRangeMaximum(25F)

        val numbers = listOf(listOf(0,0));
        val numbersSecond = listOf(listOf(0,0));

        vocDataset[0].setColor(R.color.black)
        tempDataset[0].setColor(R.color.black)

        for (data: List<Number> in numbers) {

            // turn your data into Entry objects
            vocDataset[0].addEntry(Entry(data[0].toFloat(), data[1].toFloat()));
            vocDataset[1].addEntry(Entry(data[0].toFloat(), data[1].toFloat()));
        }
        for (data: List<Number> in numbersSecond) {

            // turn your data into Entry objects
            tempDataset[0].addEntry(Entry(data[0].toFloat(), data[1].toFloat()));
            tempDataset[1].addEntry(Entry(data[0].toFloat(), data[1].toFloat()));
        }


        val lineData = LineData()
        val lineData2 = LineData()
        for (vocData in vocDataset) {
            lineData.addDataSet(vocData)

        }
        for (tempData in tempDataset){
            lineData2.addDataSet(tempData)
        }
        vocConcentrationChart.data = lineData
        vocConcentrationChart.invalidate()
        vocConcentrationChart.description.isEnabled = false

        tempChart.data = lineData2
        tempChart.invalidate()
        tempChart.description.isEnabled = false


    }
}