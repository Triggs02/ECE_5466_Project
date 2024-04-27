package com.example.fridgetempvoc

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import androidx.appcompat.app.AppCompatActivity


class WebsocketLogin : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_websocket_login)

        val btn = findViewById<Button>(R.id.button2)
        btn.setOnClickListener {
            val intent = Intent(this, MainActivity::class.java)
            intent.putExtra("url", findViewById<EditText>(R.id.editTextText).text.toString())
            startActivity(intent)
        }
    }
}